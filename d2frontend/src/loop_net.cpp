#include <d2frontend/loop_net.h>
#include <time.h> 
#include "d2frontend/loop_detector.h"

namespace D2FrontEnd {
void LoopNet::setupNetwork(std::string _lcm_uri) {
    if (!lcm.good()) {
        ROS_ERROR("LCM %s failed", _lcm_uri.c_str());
        exit(-1);
    }
    lcm.subscribe("SWARM_LOOP_IMG_DES", &LoopNet::onImgDescRecevied, this);
    lcm.subscribe("SWARM_LOOP_IMG_DES", &LoopNet::onImgDescRecevied, this);
    lcm.subscribe("SWARM_LOOP_CONN", &LoopNet::onLoopConnectionRecevied, this);
    
    lcm.subscribe("VIOKF_HEADER", &LoopNet::onImgDescHeaderRecevied, this);
    lcm.subscribe("VIOKF_LANDMARKS", &LoopNet::onLandmarkRecevied, this);
    lcm.subscribe("VIOKF_IMG_ARRAY", &LoopNet::onImgArrayRecevied, this);

    srand((unsigned)time(NULL)); 
    msg_recv_rate_callback = [&](int drone_id, float rate) {};
}

void LoopNet::broadcastVisualImageDescArray(VisualImageDescArray & image_array) {
    ImageArrayDescriptor_t fisheye_desc = image_array.toLCM();
    if (send_whole_img_desc) {
        sent_message.insert(fisheye_desc.msg_id);
        // printf("encoded image size %d\n", fisheye_desc.getEncodedSize());
        lcm.publish("VIOKF_IMG_ARRAY", &fisheye_desc);
    } else {
        for (auto & img : fisheye_desc.images) {
            if (img.landmark_num > 0) {
                broadcastImgDesc(img);
            }
        }
    }
}

void LoopNet::broadcastImgDesc(ImageDescriptor_t & img_des) {
    int64_t msg_id = rand() + img_des.timestamp.nsec;
    img_des.msg_id = msg_id;
    sent_message.insert(img_des.msg_id);
    static double sum_byte_sent = 0;
    static double sum_features = 0;
    static int count_byte_sent = 0;

    int byte_sent = 0;
    int feature_num = 0;
    for (size_t i = 0; i < img_des.landmark_num; i++ ) {
        if (img_des.landmarks[i].flag > 0) {
            feature_num ++;
        }
    }

    ImageDescriptorHeader_t img_desc_header;
    img_desc_header.timestamp = img_des.timestamp;
    img_desc_header.drone_id = img_des.drone_id;
    img_desc_header.image_desc = img_des.image_desc;
    img_desc_header.pose_drone = img_des.pose_drone;
    img_desc_header.camera_extrinsic = img_des.camera_extrinsic;
    img_desc_header.prevent_adding_db = img_des.prevent_adding_db;
    img_desc_header.msg_id = img_des.msg_id;
    img_desc_header.frame_id = img_des.frame_id;
    img_desc_header.image_desc_size = img_des.image_desc_size;
    img_desc_header.image_desc = img_des.image_desc;
    img_desc_header.feature_num = feature_num;
    img_desc_header.camera_index = img_des.camera_index;

    byte_sent += img_desc_header.getEncodedSize();
    lcm.publish("VIOKF_HEADER", &img_desc_header);
    // printf("header %d", img_desc_header.getEncodedSize());
    for (size_t i = 0; i < img_des.landmark_num; i++ ) {
        if (img_des.landmarks[i].flag > 0 || params->SEND_ALL_FEATURES) {
            LandmarkDescriptor_t lm; 
            lm.landmark = img_des.landmarks[i];
            lm.desc_len = FEATURE_DESC_SIZE;
            lm.landmark_descriptor = std::vector<float>(img_des.landmark_descriptor.data() + i *FEATURE_DESC_SIZE, 
                img_des.landmark_descriptor.data() + (i+1)*FEATURE_DESC_SIZE);
            int64_t msg_id = rand() + img_des.timestamp.nsec;
            sent_message.insert(img_des.msg_id);

            lm.msg_id = msg_id;
            lm.header_id = img_des.msg_id;
            byte_sent += lm.getEncodedSize();
            lcm.publish("VIOKF_LANDMARKS", &lm);
        }
    }

    sum_byte_sent+= byte_sent;
    sum_features+=feature_num;
    count_byte_sent ++;
    ROS_INFO("[SWARM_LOOP](%d) BD KF %d LM: %d size %d avgsize %.0f sumkB %.0f avgLM %.0f", count_byte_sent,
            img_desc_header.msg_id, feature_num, byte_sent, ceil(sum_byte_sent/count_byte_sent), sum_byte_sent/1000, ceil(sum_features/count_byte_sent));
    
    if (send_img) {
        ImageDescriptor_t img_desc_new = img_des;
        img_desc_new.landmark_descriptor_size = 0;
        img_desc_new.landmark_descriptor.clear();
        byte_sent += img_desc_new.getEncodedSize();
        ROS_INFO("[D2Frontend::LoopNet] Sending ImageDescriptor size %d with %d landmarks.local feature size %d", img_desc_new.getEncodedSize(), 
            img_desc_new.landmark_num, img_desc_new.landmark_descriptor_size);
        lcm.publish("SWARM_LOOP_IMG_DES", &img_desc_new);
    }
    // ROS_INFO("Sent Message KEYFRAME %ld with %d/%d landmarks g_desc %d total %d bytes", msg_id, feature_num,img_des.landmark_num, img_desc_header.image_desc_size, byte_sent);
}

void LoopNet::broadcastLoopConnection(swarm_msgs::LoopEdge & loop_conn) {
    auto _loop_conn = toLCMLoopEdge(loop_conn);

    sent_message.insert(_loop_conn.id);
    lcm.publish("SWARM_LOOP_CONN", &_loop_conn);
}

void LoopNet::onImgDescRecevied(const lcm::ReceiveBuffer* rbuf,
                const std::string& chan, 
                const ImageDescriptor_t* msg) {
    
    if (sent_message.find(msg->msg_id) != sent_message.end()) {
        // ROS_INFO("Receive self sent IMG message");
        return;
    }
    
    ROS_INFO("Received drone %d image from LCM!!!", msg->drone_id);
    this->imageDescCallback(*msg);
}

void LoopNet::onImgArrayRecevied(const lcm::ReceiveBuffer* rbuf,
                const std::string& chan, 
                const ImageArrayDescriptor_t* msg) {
    if (sent_message.find(msg->msg_id) == sent_message.end()) {
        frame_desc_callback(*msg);
    }
}

void LoopNet::onLandmarkRecevied(const lcm::ReceiveBuffer* rbuf,
    const std::string& chan, 
    const LandmarkDescriptor_t* msg) {
    if(msgBlocked(msg->header_id)) {
        return;
    }
    recv_lock.lock();
    updateRecvImgDescTs(msg->header_id, false);
    if (received_images.find(msg->header_id) == received_images.end()) {
        ImageDescriptor_t tmp;
        received_images[msg->header_id] = tmp; 
    }

    auto & tmp = received_images[msg->header_id];
    tmp.landmarks.emplace_back(msg->landmark);
    tmp.landmark_descriptor.insert(tmp.landmark_descriptor.end(),
        msg->landmark_descriptor.begin(),
        msg->landmark_descriptor.begin()+FEATURE_DESC_SIZE
    );
    tmp.landmark_descriptor_size = tmp.landmark_descriptor.size();
    recv_lock.unlock();
    
    scanRecvPackets();
}

void LoopNet::imageDescCallback(const ImageDescriptor_t & image){
    int64_t frame_hash = image.msg_id;

    if (received_frames.find(frame_hash) == received_frames.end()) {
        ImageArrayDescriptor_t frame_desc;
        if (params->camera_configuration == CameraConfig::STEREO_FISHEYE) {
            frame_desc.image_num = 4;
        } else if (params->camera_configuration == CameraConfig::STEREO_PINHOLE) {
            frame_desc.image_num = 2;
        } else if (params->camera_configuration == CameraConfig::PINHOLE_DEPTH) {
            frame_desc.image_num = 1;
        } else if (params->camera_configuration == CameraConfig::FOURCORNER_FISHEYE) {
            frame_desc.image_num = 4;
        }

        frame_desc.timestamp = image.timestamp;
        for (size_t i = 0; i < frame_desc.image_num; i ++) {
            if (i != image.camera_index) {
                auto img_desc = generate_null_img_desc();           
                frame_desc.images.push_back(img_desc);
            } else {
                frame_desc.images.push_back(image);
            }
        }

        frame_desc.msg_id = image.frame_id;
        frame_desc.pose_drone = image.pose_drone;
        frame_desc.landmark_num = 0;
        frame_desc.drone_id = image.drone_id;
        received_frames[frame_hash] = frame_desc;
        frame_header_recv_time[frame_hash] = msg_header_recv_time[image.msg_id];

        active_receving_frames.insert(frame_hash);
    } else {
        auto & frame_desc = received_frames[frame_hash];
        ROS_INFO("Adding image to frame %d from drone_id %d camera_index %d", frame_hash, frame_desc.drone_id, image.camera_index);
        frame_desc.images[image.camera_index] = image;
    }
}

void LoopNet::onLoopConnectionRecevied(const lcm::ReceiveBuffer* rbuf,
                const std::string& chan, 
                const LoopEdge_t* msg) {

    if (sent_message.find(msg->id) != sent_message.end()) {
        // ROS_INFO("Receive self sent Loop message");
        return;
    }
    ROS_INFO("Received Loop %d->%d from LCM!!!", msg->drone_id_a, msg->drone_id_b);    
    loopconn_callback(*msg);
}


void LoopNet::onImgDescHeaderRecevied(const lcm::ReceiveBuffer* rbuf,
    const std::string& chan, 
    const ImageDescriptorHeader_t* msg) {

    if(msgBlocked(msg->msg_id)) {
        return;
    }

    recv_lock.lock();

    ROS_INFO("ImageDescriptorHeader from drone (%d): msg_id: %ld feature num %d", msg->drone_id, msg->msg_id, msg->feature_num);
    updateRecvImgDescTs(msg->msg_id, true);

    if (received_images.find(msg->msg_id) == received_images.end()) {
        ImageDescriptor_t tmp;
        received_images[msg->msg_id] = tmp; 
        active_receving_msg.insert(msg->msg_id);
    }

    auto & tmp = received_images[msg->msg_id];
    tmp.timestamp = msg->timestamp;
    tmp.drone_id = msg->drone_id;
    tmp.image_desc_size = msg->image_desc_size;
    tmp.image_desc = msg->image_desc;
    tmp.pose_drone = msg->pose_drone;
    tmp.camera_extrinsic = msg->camera_extrinsic;
    tmp.landmark_num = msg->feature_num;
    tmp.frame_id = msg->frame_id;
    tmp.msg_id = msg->msg_id;
    tmp.prevent_adding_db = msg->prevent_adding_db;
    tmp.camera_index = msg->camera_index;
    tmp.landmark_descriptor_size = 0;

    recv_lock.unlock();
}

void LoopNet::scanRecvPackets() {
    double tnow = ros::Time::now().toSec();
    std::vector<int64_t> finish_recv;
    recv_lock.lock();
    static double sum_feature_num = 0;
    static double sum_feature_num_all = 0;
    static int sum_packets = 0;
    for (auto msg_id : active_receving_msg) {
        if (tnow - msg_header_recv_time[msg_id] > recv_period ||
            received_images[msg_id].landmark_num == received_images[msg_id].landmarks.size()) {
            sum_feature_num_all+=received_images[msg_id].landmark_num;
            sum_feature_num+=received_images[msg_id].landmarks.size();
            float cur_recv_rate = ((float)received_images[msg_id].landmarks.size())/((float) received_images[msg_id].landmark_num);
            ROS_INFO("[SWAMR_LOOP] Frame %d id %ld from drone %d, Feature %ld/%d recv_rate %.1f cur %.1f feature_desc_size %ld(%ld)", 
                sum_packets,
                msg_id, received_images[msg_id].drone_id, received_images[msg_id].landmarks.size(), received_images[msg_id].landmark_num,
                sum_feature_num/sum_feature_num_all*100,
                cur_recv_rate*100,
                received_images[msg_id].landmark_descriptor.size(), 
                received_images[msg_id].landmark_descriptor_size);
            received_images[msg_id].landmark_num = received_images[msg_id].landmarks.size();
            finish_recv.push_back(msg_id);

            sum_packets += 1;
            msg_recv_rate_callback(received_images[msg_id].drone_id, cur_recv_rate);
        }
    }

    for (auto msg_id : finish_recv) {
        blacklist.insert(msg_id);
        active_receving_msg.erase(msg_id);
    }


    for (auto _id : finish_recv) {
        auto & msg = received_images[_id];
        //Processed recevied message
        msg.landmark_num = msg.landmarks.size();
        if (msg.landmarks.size() > 0) {
            this->imageDescCallback(msg);
        }
        received_images.erase(_id);
    }

    std::vector<int64_t> finish_recv_frames;
    for (auto frame_hash : active_receving_frames) {
        int count_images = 0;
        auto & frame_desc = received_frames[frame_hash];
        for (size_t i = 0; i < frame_desc.images.size(); i++) {
            if (frame_desc.images[i].landmark_num > 0) {
                count_images++;
            }
        }

        if(tnow - frame_header_recv_time[frame_hash] > 2.0*recv_period  || count_images >= params->loopdetectorconfig->MIN_DIRECTION_LOOP) {
            finish_recv_frames.push_back(frame_hash);
        }
    }

    for (auto & frame_hash :finish_recv_frames) {
        auto & frame_desc = received_frames[frame_hash];
        active_receving_frames.erase(frame_hash);

        frame_desc.landmark_num = 0;
        for (size_t i = 0; i < frame_desc.images.size(); i ++) {
            frame_desc.landmark_num += frame_desc.images[i].landmark_num;
        }

        ROS_INFO("[SWAMR_LOOP] FFrame contains of %d images from drone %d, landmark %d", frame_desc.images.size(), frame_desc.drone_id, frame_desc.landmark_num );

        frame_desc_callback(frame_desc);
        received_frames.erase(frame_hash);
    }
    recv_lock.unlock();
}

void LoopNet::updateRecvImgDescTs(int64_t id, bool is_header) {
    if(is_header) {
        msg_header_recv_time[id] = ros::Time::now().toSec();
    }
    msg_recv_last_time[id] = ros::Time::now().toSec();
}
}