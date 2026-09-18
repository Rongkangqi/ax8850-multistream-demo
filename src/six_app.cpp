// Six AXCL hardware decode/NPU/encode streams and a 3x2 overview.
// Seg/depth transforms follow AXERA-TECH/yolo26-seg and Yolo26-Depth references.
#include <opencv2/opencv.hpp>
#include "json.hpp"
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "common/ax_system.h"
#include "pipeline/ax_pipeline.h"
#include "pipeline/ax_muxer.h"
#include "ax_plugin/ax_plugin.h"
#include "tracking/ax_bytetrack.hpp"
#include "ax_model_runner_axcl.hpp"
#include "axcl_manager.h"
#include "ax_image_internal.h"
#include "common/ax_image_processor.h"
#include "axcl_ivps.h"
using json=nlohmann::json;
using namespace axvsdk;
using Clock=std::chrono::steady_clock;
using Image=common::AxImage;
using Det=axpipeline::ai::Detection;
std::atomic<bool> running{true};
std::atomic<int> errors{0};
std::mutex log_mutex;
double seconds(){return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();}
void log(const json& j){std::lock_guard<std::mutex> l(log_mutex);std::cout<<j.dump()<<std::endl;}
void require(bool v,const std::string& s){if(!v)throw std::runtime_error(s);}
void signal_handler(int){running=false;}
cv::Scalar color(int64_t n){auto v=axpipeline::tracking::ByteTrack::ColorForTrackId(n+1);return {double(v&255),double((v>>8)&255),double((v>>16)&255)};}
void text(cv::Mat& m,const std::string& s,int x,int y,double scale=.75,cv::Scalar c={240,240,240}){
 cv::putText(m,s,{x,y},cv::FONT_HERSHEY_SIMPLEX,scale,{12,12,12},4,cv::LINE_AA);
 cv::putText(m,s,{x,y},cv::FONT_HERSHEY_SIMPLEX,scale,c,2,cv::LINE_AA);
}
std::string cname(int id,const std::string& kind){
 static const std::vector<std::string> coco={"person","bicycle","car","motorcycle","airplane","bus","train","truck","boat","traffic light","fire hydrant","stop sign","parking meter","bench","bird","cat","dog","horse","sheep","cow","elephant","bear","zebra","giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee","skis","snowboard","sports ball","kite","baseball bat","baseball glove","skateboard","surfboard","tennis racket","bottle","wine glass","cup","fork","knife","spoon","bowl","banana","apple","sandwich","orange","broccoli","carrot","hot dog","pizza","donut","cake","chair","couch","potted plant","bed","dining table","toilet","tv","laptop","mouse","remote","keyboard","cell phone","microwave","oven","toaster","sink","refrigerator","book","clock","vase","scissors","teddy bear","hair drier","toothbrush"};
 static const std::vector<std::string> helmet={"helmet","head","e-bike","bike"};
 // PCD class order from AXERA-TECH/Person_car-axera model card.
 static const std::vector<std::string> pcd={"person","car","person_cycle"};
 const auto& names=kind=="helmet"?helmet:kind=="pcd"?pcd:coco;
 return id>=0&&id<int(names.size())?names[id]:"class"+std::to_string(id);
}
void box(cv::Mat& m,const Det& d,const std::string& kind){
 int x0=std::clamp(int(d.x0),0,m.cols-1),y0=std::clamp(int(d.y0),0,m.rows-1);
 int x1=std::clamp(int(d.x1),0,m.cols-1),y1=std::clamp(int(d.y1),0,m.rows-1);
 if(x1<=x0||y1<=y0)return;
 auto c=color(d.track_id>=0?d.track_id:d.class_id);
 cv::rectangle(m,{x0,y0},{x1,y1},c,3);
 std::string label=cname(d.class_id,kind)+cv::format(" %.2f",d.score);
 if(d.track_id>=0)label+=" #"+std::to_string(d.track_id);
 text(m,label,x0,std::max(28,y0-8),.7,c);
}
// Image pixels stay in device memory from VDEC to VENC.
Image::Ptr nv12(int w=1920,int h=1080){
 common::ImageDescriptor d;d.format=common::PixelFormat::kNv12;d.width=w;d.height=h;d.strides[0]=d.strides[1]=w;
 auto p=Image::Create(d);require(bool(p),"Device image allocation");return p;
}
struct ImagePool{
 std::vector<Image::Ptr> images;
 Image::Ptr get(){for(auto& p:images)if(p.use_count()==1)return p;require(images.size()<16,"Device image pool exhausted");auto p=nv12();images.push_back(p);return p;}
};
void resize_device(const Image& src,Image& dst,int x=0,int y=0,int w=0,int h=0){
 auto a=common::internal::AxImageAccess::GetAxFrame(src),b=common::internal::AxImageAccess::GetAxFrame(dst);
 if(w){b.u64PhyAddr[0]+=y*b.u32PicStride[0]+x;b.u64PhyAddr[1]+=(y/2)*b.u32PicStride[1]+x;b.u64VirAddr[0]=b.u64VirAddr[1]=0;b.u32Width=w;b.u32Height=h;b.s16CropWidth=w;b.s16CropHeight=h;b.u32FrameSize=b.u32PicStride[0]*h*3/2;}
 AX_IVPS_ASPECT_RATIO_T ar{};ar.eMode=AX_IVPS_ASPECT_RATIO_STRETCH;
 require(AXCL_IVPS_CropResizeVpp(&a,&b,&ar)==0,"IVPS device resize/copy");
}
void upload_bgr(const cv::Mat& bgr,Image& dst){
 cv::Mat yuv,uv;cv::cvtColor(bgr,yuv,cv::COLOR_BGR2YUV_I420);int n=bgr.cols*bgr.rows;
 std::vector<cv::Mat> ps={cv::Mat(bgr.rows/2,bgr.cols/2,CV_8U,yuv.data+n),cv::Mat(bgr.rows/2,bgr.cols/2,CV_8U,yuv.data+n+n/4)};cv::merge(ps,uv);
 require(axcl_Memcpy((void*)dst.physical_address(0),yuv.data,n,AXCL_MEMCPY_HOST_TO_DEVICE,0)==0,"Background upload Y");
 require(axcl_Memcpy((void*)dst.physical_address(1),uv.data,n/2,AXCL_MEMCPY_HOST_TO_DEVICE,0)==0,"Background upload UV");
}
// Small BGR annotation maps are uploaded once per AI result, scaled on-card,
// and reused over subsequent video frames. Black is the transparent color key.
struct Overlay{
 Image::Ptr small,large;int alpha=255;bool color_key=true;
 void update(const cv::Mat& bgr,int a=255,bool key=true){
  alpha=a;color_key=key;if(!small||small->width()!=unsigned(bgr.cols)||small->height()!=unsigned(bgr.rows))small=Image::Create(common::PixelFormat::kBgr24,bgr.cols,bgr.rows);if(!large)large=Image::Create(common::PixelFormat::kBgr24,1920,1080);
  require(small&&large&&small->stride(0)==size_t(bgr.cols)*3,"Overlay allocation/stride");
  require(bgr.isContinuous(),"Contiguous overlay required");
  require(axcl_Memcpy((void*)small->physical_address(0),bgr.data,bgr.total()*3,AXCL_MEMCPY_HOST_TO_DEVICE,0)==0,"Overlay upload");
  auto x=common::internal::AxImageAccess::GetAxFrame(*small),y=common::internal::AxImageAccess::GetAxFrame(*large);
  AX_IVPS_ASPECT_RATIO_T ar{};ar.eMode=AX_IVPS_ASPECT_RATIO_STRETCH;require(AXCL_IVPS_CropResizeVgp(&x,&y,&ar)==0,"Overlay upscale");
 }
 void draw(Image& dest)const{
  auto frame=common::internal::AxImageAccess::GetAxFrame(dest);AX_OVERLAY_T o{};o.bEnable=AX_TRUE;o.nWidth=1920;o.nHeight=1080;o.nStride=large->stride(0);o.eFormat=AX_FORMAT_BGR888;o.u64PhyAddr[0]=large->physical_address(0);o.nAlpha=alpha;o.tColorKey.u16Enable=color_key?1:0;o.tColorKey.u32KeyHigh=0x030303;
  require(AXCL_IVPS_AlphaBlendingV3Vgp(&frame,&o,&frame)==0,"IVPS overlay");
 }
};
struct OverlayPool{
 std::vector<std::shared_ptr<Overlay>> images;
 std::shared_ptr<Overlay> get(const cv::Mat& m,int alpha=255,bool key=true){
  std::shared_ptr<Overlay> out;for(auto& p:images)if(p.use_count()==1){out=p;break;}if(!out){require(images.size()<8,"Overlay pool exhausted");out=std::make_shared<Overlay>();images.push_back(out);}out->update(m,alpha,key);return out;
 }
};
struct Annotation{std::shared_ptr<Overlay> mask,labels;double created=0;};
void small_text(cv::Mat& m,const std::string&s,int x,int y,double scale=.42,cv::Scalar c={240,240,240}){
 cv::putText(m,s,{x,y},cv::FONT_HERSHEY_SIMPLEX,scale,{12,12,12},3,cv::LINE_AA);cv::putText(m,s,{x,y},cv::FONT_HERSHEY_SIMPLEX,scale,c,1,cv::LINE_AA);
}
void small_box(cv::Mat& m,const Det& d,const std::string& kind){
 int x0=std::clamp(int(d.x0/3),0,639),y0=std::clamp(int(d.y0/3),0,359),x1=std::clamp(int(d.x1/3),0,639),y1=std::clamp(int(d.y1/3),0,359);
 if(x1<=x0||y1<=y0)return;
 auto c=color(d.track_id>=0?d.track_id:d.class_id);cv::rectangle(m,{x0,y0},{x1,y1},c,1);
 std::string label=cname(d.class_id,kind);if(d.track_id>=0)label+=" #"+std::to_string(d.track_id);small_text(m,label,x0,std::max(12,y0-3),.32,c);
}
struct Plugin{
 void* so=nullptr;ax_plugin_handle_t handle=nullptr;
 decltype(&ax_plugin_infer) infer=nullptr;decltype(&ax_plugin_release_result) release=nullptr;decltype(&ax_plugin_deinit) deinit=nullptr;
 void init(const std::string& path,const json& options){
  so=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);if(!so)throw std::runtime_error(dlerror());
  auto version=(decltype(&ax_plugin_get_api_version))dlsym(so,"ax_plugin_get_api_version");
  auto initfn=(decltype(&ax_plugin_init))dlsym(so,"ax_plugin_init");
  infer=(decltype(infer))dlsym(so,"ax_plugin_infer");release=(decltype(release))dlsym(so,"ax_plugin_release_result");deinit=(decltype(deinit))dlsym(so,"ax_plugin_deinit");
  require(version&&version()==2&&initfn&&infer&&release&&deinit,"Bad plugin ABI");require(initfn(options.dump().c_str(),0,&handle)==0,"Plugin init failed: "+path);
 }
 std::vector<Det> run(Image& f){
  ax_plugin_image_view_t v{};v.format=(ax_plugin_pixel_format_e)f.format();v.width=f.width();v.height=f.height();v.plane_count=f.plane_count();v.memory_type=AX_PLUGIN_MEMORY_TYPE_AXCL_DEVICE;
  for(size_t i=0;i<v.plane_count;i++){v.strides[i]=f.stride(i);v.physical_addrs[i]=f.physical_address(i);v.virtual_addrs[i]=f.virtual_address(i);v.block_ids[i]=f.block_id(i);}
  ax_plugin_det_result_t r{};int rc=infer(handle,&v,&r);if(rc){release(handle,&r);throw std::runtime_error("Plugin inference failed: "+std::to_string(rc));}
  std::vector<Det> out;for(size_t i=0;i<r.det_count;i++){auto d=r.dets[i];out.push_back({d.x0,d.y0,d.x1,d.y1,d.score,d.class_id,d.track_id});}release(handle,&r);return out;
 }
 ~Plugin(){if(handle)deinit(handle);/* Keep DSO loaded until TLS context destructors finish. */}
};
struct TensorModel{
 ax_runner_axcl runner;bool ready=false;std::vector<char> bytes;int h=0,w=0;std::string kind;float gain=1;int left=0,top=0,nw=0,nh=0;
 void init(const std::string& path,const std::string& k){
  kind=k;std::ifstream f(path,std::ios::binary);require(bool(f),"Missing model "+path);bytes.assign(std::istreambuf_iterator<char>(f),{});
  require(runner.init(bytes.data(),bytes.size(),0)==0,"NPU model init failed "+path);ready=true;
  require(runner.get_num_inputs()==1,"Model input count");auto t=runner.get_input(0);
  require(t.vShape.size()==4&&t.vShape[0]==1&&t.vShape[3]==3,"Model must be NHWC RGB uint8");h=t.vShape[1];w=t.vShape[2];require(t.nSize==h*w*3,"Unexpected input datatype");
  json shapes=json::array();for(int i=0;i<runner.get_num_outputs();i++){auto o=runner.get_output(i);shapes.push_back({{"name",o.sName},{"shape",o.vShape},{"bytes",o.nSize}});}
  log({{"event","model"},{"kind",kind},{"input",t.vShape},{"outputs",shapes}});
  runner.set_auto_sync_before_inference(false);runner.set_auto_sync_after_inference(kind!="depth");
 }
 void run(const Image& image){
  gain=std::min(float(w)/image.width(),float(h)/image.height());
  nw=kind=="depth"?int(std::round(image.width()*gain)):int(image.width()*gain);nh=kind=="depth"?int(std::round(image.height()*gain)):int(image.height()*gain);
  left=kind=="depth"?(w-nw)/2:0;top=kind=="depth"?(h-nh)/2:0;
  auto t=runner.get_input(0);common::ImageDescriptor d;d.format=common::PixelFormat::kRgb24;d.width=w;d.height=h;d.strides[0]=w*3;
  std::array<common::ExternalImagePlane,3> planes{};planes[0].physical_address=t.phyAddr;planes[0].virtual_address=(void*)t.phyAddr;
  auto target=Image::WrapExternal(d,planes);require(bool(target),"NPU input wrap");
  common::ImageProcessRequest request;request.output_image=d;request.resize.mode=common::ResizeMode::kKeepAspectRatio;request.resize.background_color=kind=="depth"?0x727272:0x7f7f7f;
  request.resize.horizontal_align=request.resize.vertical_align=kind=="depth"?common::ResizeAlign::kCenter:common::ResizeAlign::kStart;
  auto processor=common::CreateImageProcessor();require(processor&&processor->Process(image,request,*target),"NPU hardware preprocessing");
  require(runner.inference()==0,"NPU inference failed");
  if(kind=="depth") {
   // Depth rendering reads only the unpadded source region. Copy those rows
   // into their original tensor offsets, preserving all values used below.
   require(runner.get_num_outputs()==1,"Depth output count");auto o=runner.get_output(0);auto s=o.vShape;
   require(s.size()==4&&s[0]==1&&s[1]==1&&size_t(o.nSize)==size_t(s[2])*s[3]*4,"Depth output layout");
   int y=std::round(float(top)*s[2]/h),rows=std::round(float(nh)*s[2]/h);
   rows=std::min(rows,int(s[2])-y);size_t offset=size_t(y)*s[3]*4,bytes=size_t(rows)*s[3]*4;
   require(y>=0&&rows>0&&offset+bytes<=size_t(o.nSize),"Depth crop bounds");
   require(axcl_Memcpy(static_cast<char*>(o.pVirAddr)+offset,(void*)(o.phyAddr+offset),bytes,AXCL_MEMCPY_DEVICE_TO_HOST,0)==0,"Depth cropped D2H failed");
  }
 }
 cv::Mat depth(){
  require(runner.get_num_outputs()==1,"Depth outputs");auto o=runner.get_output(0);auto s=o.vShape;
  require(s.size()==4&&s[0]==1&&s[1]==1&&o.nSize==int(s[2]*s[3]*4),"Depth layout");
  cv::Mat raw(s[2],s[3],CV_32F,o.pVirAddr);int x=std::round(float(left)*raw.cols/w),y=std::round(float(top)*raw.rows/h);
  int rw=std::round(float(nw)*raw.cols/w),rh=std::round(float(nh)*raw.rows/h);
  cv::Mat d=raw(cv::Rect(x,y,std::min(rw,raw.cols-x),std::min(rh,raw.rows-y))).clone();
  std::vector<float> pool;pool.reserve(d.total());
  for(auto it=d.begin<float>();it!=d.end<float>();++it){*it=std::isfinite(*it)&&*it>0?1.f/(*it):0.f;if(*it>0)pool.push_back(*it);}
  require(!pool.empty(),"Depth has no finite positive values");
  std::nth_element(pool.begin(),pool.begin()+pool.size()*2/100,pool.end());float lo=pool[pool.size()*2/100];
  std::nth_element(pool.begin(),pool.begin()+pool.size()*98/100,pool.end());float hi=std::max(lo+1e-6f,pool[pool.size()*98/100]);
  cv::Mat gray,heat;d.convertTo(gray,CV_8U,255/(hi-lo),-lo*255/(hi-lo));cv::applyColorMap(gray,heat,cv::COLORMAP_INFERNO);heat.setTo(cv::Scalar::all(0),d<=0);return heat;

 }
 struct Candidate{Det d;std::vector<float> coeff;};
 std::vector<Det> segmentation(cv::Mat& mask_colors){
  require(runner.get_num_outputs()==10,"YOLO26 seg requires 10 output tensors");std::vector<Candidate> cand;
  const float threshold=.3f,rawth=std::log(threshold/(1-threshold));
  for(int k=0;k<3;k++){
   auto b=runner.get_output(3*k),c=runner.get_output(3*k+1),m=runner.get_output(3*k+2);
   require(b.vShape.size()==4&&b.vShape[3]==4&&c.vShape.size()==4&&c.vShape[3]==80&&m.vShape.size()==4&&m.vShape[3]==32,"Unexpected seg head layout");
   int gh=b.vShape[1],gw=b.vShape[2],stride=8<<k;require(gh*stride==h&&gw*stride==w,"Seg head grid");
   require(b.nSize==gh*gw*4*4&&c.nSize==gh*gw*80*4&&m.nSize==gh*gw*32*4,"Seg tensor byte size");
   auto bp=(float*)b.pVirAddr,cp=(float*)c.pVirAddr,mp=(float*)m.pVirAddr;
   for(int i=0;i<gh*gw;i++){auto cl=cp+i*80;int id=std::max_element(cl,cl+80)-cl;if(cl[id]<rawth)continue;
    float ax=i%gw+.5f,ay=i/gw+.5f;auto v=bp+i*4;Candidate a;a.d={(ax-v[0])*stride,(ay-v[1])*stride,(ax+v[2])*stride,(ay+v[3])*stride,1.f/(1+std::exp(-cl[id])),id,-1};a.coeff.assign(mp+i*32,mp+(i+1)*32);cand.push_back(std::move(a));}
  }
  std::sort(cand.begin(),cand.end(),[](const auto&a,const auto&b){return a.d.score>b.d.score;});if(cand.size()>300)cand.resize(300);
  std::vector<Candidate> keep;
  for(auto& a:cand){bool suppress=false;for(auto& b:keep){if(a.d.class_id!=b.d.class_id)continue;auto A=a.d,B=b.d;float inter=std::max(0.f,std::min(A.x1,B.x1)-std::max(A.x0,B.x0))*std::max(0.f,std::min(A.y1,B.y1)-std::max(A.y0,B.y0));float den=(A.x1-A.x0)*(A.y1-A.y0)+(B.x1-B.x0)*(B.y1-B.y0)-inter;if(den>0&&inter/den>.7f){suppress=true;break;}}if(!suppress)keep.push_back(a);if(keep.size()>=30)break;}
  auto p=runner.get_output(9);require(p.vShape.size()==4&&p.vShape[1]==32,"Seg proto must be NCHW");int ph=p.vShape[2],pw=p.vShape[3];require(p.nSize==32*ph*pw*4,"Proto bytes");cv::Mat proto(32,ph*pw,CV_32F,p.pVirAddr);
  std::vector<Det> out;cv::Mat colors(nh,nw,CV_8UC3,cv::Scalar::all(0));
  for(auto& a:keep){cv::Mat coeff(1,32,CV_32F,a.coeff.data());
   int x0=std::clamp(int(a.d.x0*pw/w),0,pw),y0=std::clamp(int(a.d.y0*ph/h),0,ph),x1=std::clamp(int(a.d.x1*pw/w),0,pw),y1=std::clamp(int(a.d.y1*ph/h),0,ph);
   if(x1<=x0||y1<=y0)continue;
   // The reference zeros everything outside this prototype ROI. Compute
   // exactly the retained pixels instead of multiplying all 160x160 pixels.
   cv::Rect roi(x0,y0,x1-x0,y1-y0);cv::Mat selected(32,roi.area(),CV_32F);
   for(int k=0;k<32;k++){cv::Mat plane(ph,pw,CV_32F,proto.ptr<float>(k));cv::Mat target=selected.row(k).reshape(1,roi.height);plane(roi).copyTo(target);}
   cv::Mat flat;cv::gemm(coeff,selected,1,cv::noArray(),0,flat);
   cv::Mat cropped=cv::Mat::zeros(ph,pw,CV_32F);flat.reshape(1,roi.height).copyTo(cropped(roi));
   cv::Mat full,binary;cv::resize(cropped,full,{w,h});cv::compare(full(cv::Rect(0,0,nw,nh)),.5,binary,cv::CMP_GT);
   colors.setTo(color(a.d.class_id),binary);
   auto d=a.d;d.x0/=gain;d.y0/=gain;d.x1/=gain;d.y1/=gain;out.push_back(d);
  }
  mask_colors=colors;return out;
 }
 ~TensorModel(){if(ready)runner.deinit();}
};
struct Output{
 std::unique_ptr<codec::VideoEncoder> encoder;std::unique_ptr<pipeline::Muxer> mux;double fps=30;
 std::atomic<uint64_t> mux_fail{0},submit_fail{0};
 void init(const std::string& uri,double rate,int bitrate){
  fps=rate;mux=pipeline::CreateMuxer();pipeline::MuxerConfig mc;mc.stream={codec::VideoCodecType::kH264,1920,1080,fps};mc.uris={uri};require(mux->Open(mc),"Muxer open failed "+uri);
  encoder=codec::CreateVideoEncoder();codec::VideoEncoderConfig ec;ec.codec=codec::VideoCodecType::kH264;ec.width=1920;ec.height=1080;ec.device_id=0;ec.frame_rate=fps;ec.bitrate_kbps=bitrate;ec.gop=int(std::round(fps*2));ec.input_queue_depth=2;ec.overflow_policy=codec::QueueOverflowPolicy::kDropOldest;
  require(encoder->Open(ec),"VENC open");encoder->SetPacketCallback([this](codec::EncodedPacket p){if(!mux->SubmitPacket(std::move(p)))++mux_fail;});require(encoder->Start(),"VENC start");
 }
 void submit(const Image::Ptr& image,uint64_t seq){
  std::array<common::ExternalImagePlane,3> planes{};for(size_t i=0;i<image->plane_count();i++){planes[i].physical_address=image->physical_address(i);planes[i].virtual_address=image->virtual_address(i);}
  auto send=Image::WrapExternal(image->descriptor(),planes,image);require(bool(send),"VENC wrap");
  auto info=common::internal::AxImageAccess::MutableAxFrame(send.get());info->u64PTS=uint64_t(std::llround(seq*1000000/fps));info->u32TimeRef=seq;info->u64SeqNum=seq;
  if(!encoder->SubmitFrame(std::move(send)))++submit_fail;
 }
 void close(){if(encoder){encoder->Stop();encoder->Close();encoder.reset();}if(mux){mux->Close();mux.reset();}}
 ~Output(){close();}
};
struct Channel{
 json cfg;int index;double fps;std::unique_ptr<pipeline::Pipeline> input;Output output;Plugin plugin;TensorModel model;
 std::unique_ptr<axpipeline::tracking::ByteTrack> tracker;std::mutex m;std::condition_variable cv;Image::Ptr pending;Image::Ptr latest;std::deque<std::pair<uint64_t,Image::Ptr>> render_queue;OverlayPool mask_pool,label_pool;std::shared_ptr<Annotation> annotation;uint64_t raw_sequence=0;ImagePool pool;
 std::thread worker,renderer;std::atomic<uint64_t> rendered{0},render_replaced{0};std::atomic<double> render_ms{0},render_updated{0};std::atomic<uint64_t> frames{0},replaced{0};std::atomic<double> updated{0},infer_ms{0};std::atomic<int> objects{0};
 std::atomic<int> lr{0},rl{0};struct Cross{int side=0;bool counted=false;double seen=0;};std::unordered_map<int64_t,Cross> crosses;
 uint64_t loop_index=0;
 Channel(json j,int i,double f):cfg(j),index(i),fps(j.value("source_fps",f)){}
 void init(){
  std::string kind=cfg["kind"];
  if(kind=="depth"||kind=="seg")model.init(cfg["model"],kind);else plugin.init(cfg["plugin"],cfg["plugin_options"]);
  if(kind=="pcd"||kind=="count"||cfg.value("enable_tracking",false)){axpipeline::tracking::ByteTrackOptions o;o.frame_rate=int(fps);o.track_buffer=int(fps*3);o.smooth=true;tracker=std::make_unique<axpipeline::tracking::ByteTrack>(o);}
  output.init(cfg["output"],fps,3500);input=pipeline::CreatePipeline();pipeline::PipelineConfig pc;pc.device_id=0;pc.input.uri=cfg["input"];pc.input.realtime_playback=true;pc.input.loop_playback=true;
  pc.frame_output.output_image.format=common::PixelFormat::kNv12;pc.frame_output.output_image.width=1920;pc.frame_output.output_image.height=1080;
  require(input->Open(pc),"Decoder open failed");input->SetFrameCallback([this](Image::Ptr f){std::lock_guard<std::mutex> l(m);if(pending)++replaced;pending=f;if(render_queue.size()>=4){render_queue.pop_front();++render_replaced;}render_queue.emplace_back(raw_sequence++,std::move(f));cv.notify_all();});
 }
 void start(){worker=std::thread([this]{run();});renderer=std::thread([this]{render();});require(input->Start(),"Input start failed");}
 void count(std::vector<Det>& ds){
  ds.erase(std::remove_if(ds.begin(),ds.end(),[](auto&d){return d.class_id!=1&&d.class_id!=2&&d.class_id!=3&&d.class_id!=5&&d.class_id!=7;}),ds.end());
  auto tracks=tracker->Update(ds);ds.clear();double now=seconds();float line=cfg.value("line_x",.55)*1920;float dead=cfg.value("line_deadband",.015)*1920;
  for(auto&t:tracks){ds.push_back({t.x0,t.y0,t.x1,t.y1,t.score,t.class_id,t.track_id});auto& a=crosses[t.track_id];a.seen=now;float x=(t.x0+t.x1)*.5f;int side=x<line-dead?-1:x>line+dead?1:0;
   if(side&&a.side&&side!=a.side&&!a.counted){if(side>0)++lr;else ++rl;a.counted=true;log({{"event","crossing"},{"id",t.track_id},{"direction",side>0?"left_to_right":"right_to_left"},{"lr",lr.load()},{"rl",rl.load()}});}if(side)a.side=side;
  }
  for(auto it=crosses.begin();it!=crosses.end();)if(now-it->second.seen>10)it=crosses.erase(it);else++it;

 }
 void run(){
  try{std::string kind=cfg["kind"];
   while(running){Image::Ptr frame;
    {std::unique_lock<std::mutex> l(m);cv.wait_for(l,std::chrono::milliseconds(200),[this]{return pending||!running;});if(!running)break;if(!pending)continue;frame=std::move(pending);}
    double begin=seconds();std::vector<Det> ds;cv::Mat mask;auto ann=std::make_shared<Annotation>();
    if(kind=="depth"||kind=="seg"){model.run(*frame);if(kind=="depth")mask=model.depth();else ds=model.segmentation(mask);}
    else ds=plugin.run(*frame);
    if(kind=="vehicle")ds.erase(std::remove_if(ds.begin(),ds.end(),[](auto&d){return d.class_id!=0&&d.class_id!=1&&d.class_id!=2&&d.class_id!=3&&d.class_id!=5&&d.class_id!=7&&d.class_id!=9&&d.class_id!=11;}),ds.end());
    if(kind=="count"){
     auto span=cfg.value("source_loop_us",uint64_t(0));auto pts=common::internal::AxImageAccess::GetAxFrame(*frame).u64PTS;
     if(span&&pts/span>loop_index){loop_index=pts/span;axpipeline::tracking::ByteTrackOptions o;o.frame_rate=int(fps);o.track_buffer=int(fps*3);o.smooth=true;tracker=std::make_unique<axpipeline::tracking::ByteTrack>(o);crosses.clear();log({{"event","count_loop_reset"},{"loop",loop_index}});}
     count(ds);
    }else if(tracker){
     auto detections=ds;auto tracks=tracker->Update(ds);ds.clear();
     for(auto&t:tracks)ds.push_back({t.x0,t.y0,t.x1,t.y1,t.score,t.class_id,t.track_id});
     // ByteTrack only returns confirmed tracks. Keep valid, unmatched detector
     // results visible without inventing an ID for small or newly seen objects.
     if(cfg.value("show_unconfirmed_detections",false))for(auto& d:detections){
      bool matched=false;for(auto& t:ds){
       if(t.track_id<0||d.class_id!=t.class_id)continue;
       float inter=std::max(0.f,std::min(d.x1,t.x1)-std::max(d.x0,t.x0))*std::max(0.f,std::min(d.y1,t.y1)-std::max(d.y0,t.y0));
       float area=(d.x1-d.x0)*(d.y1-d.y0)+(t.x1-t.x0)*(t.y1-t.y0)-inter;
       if(area>0&&inter/area>.3f){matched=true;break;}
      }
      if(!matched){d.track_id=-1;ds.push_back(d);}
     }
    }
    if(!mask.empty())ann->mask=mask_pool.get(mask,kind=="depth"?166:115,kind!="depth");
    cv::Mat labels(360,640,CV_8UC3,cv::Scalar::all(0));for(auto& d:ds)small_box(labels,d,kind);
    objects=ds.size();++frames;
    cv::rectangle(labels,{0,0},{640,29},{18,24,34},-1);small_text(labels,std::to_string(index+1)+"  "+cfg["title"].get<std::string>(),8,13,.4);
    small_text(labels,cv::format("AI #%llu | objects %d | video independent of AI",(unsigned long long)frames.load(),objects.load()),8,25,.31);
    if(kind=="count"){int line=cfg.value("line_x",.4)*640;cv::line(labels,{line,30},{line,359},{0,240,255},1);small_text(labels,"L->R "+std::to_string(lr)+"   R->L "+std::to_string(rl),10,48,.44);small_text(labels,"Image-line crossings | moving camera | loops accumulate",8,347,.32);}
    if(kind=="depth")small_text(labels,"Relative depth | bright=near | not calibrated metres",8,347,.34);
    ann->labels=label_pool.get(labels);ann->created=seconds();infer_ms=(seconds()-begin)*1000;
    {std::lock_guard<std::mutex> l(m);updated=ann->created;annotation.swap(ann);}

   }
  }catch(const std::exception& e){log({{"event","channel_error"},{"channel",cfg["name"]},{"error",e.what()}});++errors;running=false;}
 }
 void render(){
  try{require(axcl_Dev_Init(0)==0,"Render device context");while(running){Image::Ptr src;uint64_t seq;std::shared_ptr<Annotation> ann;
   {std::unique_lock<std::mutex> l(m);cv.wait_for(l,std::chrono::milliseconds(200),[this]{return !render_queue.empty()||!running;});if(!running)break;if(render_queue.empty())continue;src=std::move(render_queue.front().second);seq=render_queue.front().first;render_queue.pop_front();ann=annotation;}
   double begin=seconds();auto dest=pool.get();resize_device(*src,*dest);
   // Never draw on decoder buffers: the NPU may still be reading them.
   if(ann){if(ann->mask)ann->mask->draw(*dest);if(ann->labels)ann->labels->draw(*dest);}
   output.submit(dest,seq);{std::lock_guard<std::mutex> l(m);latest=dest;}++rendered;render_updated=seconds();render_ms=(seconds()-begin)*1000;
  }}catch(const std::exception&e){log({{"event","render_error"},{"channel",cfg["name"]},{"error",e.what()}});++errors;running=false;}
 }
 void stop(){if(input)input->Stop();cv.notify_all();if(worker.joinable())worker.join();if(renderer.joinable())renderer.join();output.close();{std::lock_guard<std::mutex> l(m);pending.reset();render_queue.clear();latest.reset();annotation.reset();}pool.images.clear();if(input){input->Close();input.reset();}}

 ~Channel(){stop();}
};
int run_application(int argc,char**argv){
 std::vector<std::unique_ptr<Channel>> channels;Output overview;ImagePool overview_pool;Image::Ptr background;double duration=argc>2?std::stod(argv[2]):0;
 try{
  require(argc>=2,"Usage: six_app config.json [duration_seconds]");std::ifstream f(argv[1]);json config=json::parse(f);double fps=config.value("output_fps",30.);
  common::SystemOptions sys;sys.backend=common::BackendType::kAxcl;sys.device_id=0;require(common::InitializeSystem(sys),"SDK init");require(axcl_Dev_Init(0)==0,"AXCL init");
  cv::Mat bg(1080,1920,CV_8UC3,cv::Scalar(15,21,29));
  for(auto& c:config["channels"]){auto p=std::make_unique<Channel>(c,channels.size(),fps);p->init();int i=channels.size();auto cell=bg(cv::Rect((i%3)*640,(i/3)*540,640,540));text(cell,std::to_string(i+1)+"  "+c["title"].get<std::string>(),16,37,.65);text(cell,"AX8850 device-side video / OSD / H.264",16,492,.48,{112,222,180});text(cell,"Video and AI update independently",16,520,.48,{151,164,181});channels.push_back(std::move(p));}
  background=nv12();upload_bgr(bg,*background);overview.init(config["overview"],fps,8000);for(auto& c:channels)c->start();double started=seconds(),lastlog=started;auto tick=Clock::now();std::vector<uint64_t> before(channels.size(),0);uint64_t seq=0;
  while(running&&(!duration||seconds()-started<duration)){
   auto canvas=overview_pool.get();resize_device(*background,*canvas);bool all=true;double now=seconds();
   for(size_t i=0;i<channels.size();i++){auto& c=*channels[i];Image::Ptr view;{std::lock_guard<std::mutex> l(c.m);view=c.latest;}
    if(view){resize_device(*view,*canvas,(i%3)*640,(i/3)*540+100,640,360);if(now-c.render_updated>5)throw std::runtime_error("Stale video "+c.cfg["name"].get<std::string>());}
    else{all=false;if(now-started>30)throw std::runtime_error("No video "+c.cfg["name"].get<std::string>());}
    if(c.updated>0){if(now-c.updated>5)throw std::runtime_error("Stale AI "+c.cfg["name"].get<std::string>());}else if(now-started>30)throw std::runtime_error("No AI "+c.cfg["name"].get<std::string>());
   }
   if(all)overview.submit(canvas,seq);
   ++seq;
   if(now-lastlog>=10){json list=json::array();for(size_t i=0;i<channels.size();i++){auto& c=*channels[i];auto e=c.output.encoder->GetStats();list.push_back({{"name",c.cfg["name"]},{"decoded",c.input->GetStats().decoded_frames},{"inferred",c.frames.load()},{"infer_fps",(c.frames-before[i])/(now-lastlog)},{"process_ms",c.infer_ms.load()},{"objects",c.objects.load()},{"result_age",std::max(0.,seconds()-c.updated.load())},{"rendered",c.rendered.load()},{"render_ms",c.render_ms.load()},{"render_replaced",c.render_replaced.load()},{"encoded",e.encoded_packets},{"dropped",e.dropped_frames},{"mux_errors",c.output.mux_fail.load()},{"submit_errors",c.output.submit_fail.load()},{"lr",c.lr.load()},{"rl",c.rl.load()}});before[i]=c.frames;}
    auto es=overview.encoder->GetStats();log({{"event","stats"},{"elapsed",now-started},{"channels",list},{"overview_encoded",es.encoded_packets},{"overview_dropped",es.dropped_frames},{"overview_mux_errors",overview.mux_fail.load()}});lastlog=now;
   }
   tick+=std::chrono::microseconds(int(1000000/fps));if(tick<Clock::now()-std::chrono::milliseconds(100))tick=Clock::now();std::this_thread::sleep_until(tick);
  }
 }catch(const std::exception&e){log({{"event","fatal"},{"error",e.what()}});++errors;}
 running=false;for(auto& c:channels)c->stop();overview.close();channels.clear();overview_pool.images.clear();background.reset();axcl_Dev_Exit(0);log({{"event","exit"},{"errors",errors.load()}});return errors?1:0;
}
int main(int argc,char**argv){
 std::signal(SIGINT,signal_handler);std::signal(SIGTERM,signal_handler);cv::setNumThreads(1);
 int rc=1;std::thread app([&]{rc=run_application(argc,argv);});app.join();common::ShutdownSystem();return rc;
}
