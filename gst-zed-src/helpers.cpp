static void gst_zedsrc_setup_runtime_parameters(GstZedSrc *src, sl::RuntimeParameters &zedRtParams) {
    GST_TRACE_OBJECT(src, "CAMERA RUNTIME PARAMETERS");
    if (src->depth_mode == static_cast<gint>(sl::DEPTH_MODE::NONE) && !src->pos_tracking) {
        zedRtParams.enable_depth = false;
    } else {
        zedRtParams.enable_depth = true;
    }
    zedRtParams.confidence_threshold = src->confidence_threshold;
    zedRtParams.texture_confidence_threshold = src->texture_confidence_threshold;
    zedRtParams.measure3D_reference_frame =
        static_cast<sl::REFERENCE_FRAME>(src->measure3D_reference_frame);
    zedRtParams.enable_fill_mode = src->fill_mode;
    zedRtParams.remove_saturated_areas = src->remove_saturated_areas == TRUE;

    // Runtime exposure control
    if (src->exposure_gain_updated) {
        if (src->aec_agc == FALSE) {
            // Manual exposure control
            src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC, src->aec_agc);
            src->zed.setCameraSettings(sl::VIDEO_SETTINGS::EXPOSURE, src->exposure);
            src->zed.setCameraSettings(sl::VIDEO_SETTINGS::GAIN, src->gain);
            GST_INFO(" Runtime EXPOSURE %d - GAIN %d", src->exposure, src->gain);
            src->exposure_gain_updated = FALSE;
        } else {
            // Auto exposure control
            if (src->aec_agc_roi_x != -1 && src->aec_agc_roi_y != -1 && src->aec_agc_roi_w != -1 &&
                src->aec_agc_roi_h != -1) {
                sl::Rect roi;
                roi.x = src->aec_agc_roi_x;
                roi.y = src->aec_agc_roi_y;
                roi.width = src->aec_agc_roi_w;
                roi.height = src->aec_agc_roi_h;
                sl::SIDE side = static_cast<sl::SIDE>(src->aec_agc_roi_side);
                GST_INFO(" Runtime AEC_AGC_ROI: (%d,%d)-%dx%d - Side: %d", src->aec_agc_roi_x,
                         src->aec_agc_roi_y, src->aec_agc_roi_w, src->aec_agc_roi_h,
                         src->aec_agc_roi_side);
                src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC, src->aec_agc);
                src->zed.setCameraSettings(sl::VIDEO_SETTINGS::AEC_AGC_ROI, roi, side);
                src->exposure_gain_updated = FALSE;
            }
        }
    }
}

static void gst_zedsrc_attach_metadata(GstZedSrc *src, GstBuffer *buf, GstClockTime clock_time) {
    ZedInfo info;
    ZedPose pose;
    ZedSensors sens;
    ZedObjectData obj_data[GST_ZEDSRC_MAX_OBJECTS] = {0};
    guint8 obj_count = 0;
    guint64 offset = 0;
    sl::ERROR_CODE ret;

    sl::CameraInformation cam_info;
    sl::ObjectDetectionRuntimeParameters od_rt_params;
    std::vector<sl::OBJECT_CLASS> class_filter;
    std::map<sl::OBJECT_CLASS, float> class_det_conf;
    sl::Objects det_objs;
    sl::BodyTrackingRuntimeParameters bt_rt_params;
    sl::Bodies bodies;

    // ----> Info metadata
    cam_info = src->zed.getCameraInformation();
    info.cam_model = (gint) cam_info.camera_model;
    info.stream_type = src->stream_type;
    info.grab_single_frame_width = cam_info.camera_configuration.resolution.width;
    info.grab_single_frame_height = cam_info.camera_configuration.resolution.height;
    if (info.grab_single_frame_height == 752 || info.grab_single_frame_height == 1440 ||
        info.grab_single_frame_height == 2160 || info.grab_single_frame_height == 2484) {
        info.grab_single_frame_height /= 2;   // Only half buffer size if the stream is composite
    }
    // <---- Info metadata

    // ----> Positional Tracking metadata
    if (src->pos_tracking) {
        sl::Pose cam_pose;
        sl::POSITIONAL_TRACKING_STATE state = src->zed.getPosition(cam_pose);

        sl::Translation pos = cam_pose.getTranslation();
        pose.pose_avail = TRUE;
        pose.pos_tracking_state = static_cast<int>(state);
        pose.pos[0] = pos(0);
        pose.pos[1] = pos(1);
        pose.pos[2] = pos(2);

        sl::Orientation orient = cam_pose.getOrientation();
        sl::float3 euler = orient.getRotationMatrix().getEulerAngles();
        pose.orient[0] = euler[0];
        pose.orient[1] = euler[1];
        pose.orient[2] = euler[2];
    } else {
        pose.pose_avail = FALSE;
        pose.pos_tracking_state = static_cast<int>(sl::POSITIONAL_TRACKING_STATE::OFF);
        pose.pos[0] = 0.0;
        pose.pos[1] = 0.0;
        pose.pos[2] = 0.0;
        pose.orient[0] = 0.0;
        pose.orient[1] = 0.0;
        pose.orient[2] = 0.0;
    }
    // <---- Positional Tracking

    // ----> Sensors metadata
    if (src->zed.getCameraInformation().camera_model != sl::MODEL::ZED) {
        sens.sens_avail = TRUE;
        sens.imu.imu_avail = TRUE;

        sl::SensorsData sens_data;
        src->zed.getSensorsData(sens_data, sl::TIME_REFERENCE::IMAGE);

        sens.imu.acc[0] = sens_data.imu.linear_acceleration.x;
        sens.imu.acc[1] = sens_data.imu.linear_acceleration.y;
        sens.imu.acc[2] = sens_data.imu.linear_acceleration.z;
        sens.imu.gyro[0] = sens_data.imu.angular_velocity.x;
        sens.imu.gyro[1] = sens_data.imu.angular_velocity.y;
        sens.imu.gyro[2] = sens_data.imu.angular_velocity.z;

        if (src->zed.getCameraInformation().camera_model != sl::MODEL::ZED_M) {
            sens.mag.mag_avail = TRUE;
            sens.mag.mag[0] = sens_data.magnetometer.magnetic_field_calibrated.x;
            sens.mag.mag[1] = sens_data.magnetometer.magnetic_field_calibrated.y;
            sens.mag.mag[2] = sens_data.magnetometer.magnetic_field_calibrated.z;
            sens.env.env_avail = TRUE;

            float temp;
            sens_data.temperature.get(sl::SensorsData::TemperatureData::SENSOR_LOCATION::BAROMETER,
                                      temp);
            sens.env.temp = temp;
            sens.env.press = sens_data.barometer.pressure * 1e-2;

            float tempL, tempR;
            sens_data.temperature.get(
                sl::SensorsData::TemperatureData::SENSOR_LOCATION::ONBOARD_LEFT, tempL);
            sens_data.temperature.get(
                sl::SensorsData::TemperatureData::SENSOR_LOCATION::ONBOARD_RIGHT, tempR);
            sens.temp.temp_avail = TRUE;
            sens.temp.temp_cam_left = tempL;
            sens.temp.temp_cam_right = tempR;
        } else {
            sens.mag.mag_avail = FALSE;
            sens.env.env_avail = FALSE;
            sens.temp.temp_avail = FALSE;
        }
    } else {
        sens.sens_avail = FALSE;
        sens.imu.imu_avail = FALSE;
        sens.mag.mag_avail = FALSE;
        sens.env.env_avail = FALSE;
        sens.temp.temp_avail = FALSE;
    }
    // <---- Sensors metadata

    // ----> Object detection metadata
    if (src->object_detection) {
        GST_LOG_OBJECT(src, "Object Detection enabled");

        od_rt_params.detection_confidence_threshold = src->od_det_conf;

        class_filter.clear();
        if (src->od_person_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::PERSON);
        if (src->od_vehicle_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::VEHICLE);
        if (src->od_animal_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::ANIMAL);
        if (src->od_bag_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::BAG);
        if (src->od_electronics_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::ELECTRONICS);
        if (src->od_fruit_vegetable_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::FRUIT_VEGETABLE);
        if (src->od_sport_conf > 0.0f)
            class_filter.push_back(sl::OBJECT_CLASS::SPORT);
        od_rt_params.object_class_filter = class_filter;

        class_det_conf.clear();
        class_det_conf[sl::OBJECT_CLASS::PERSON] = src->od_person_conf;
        class_det_conf[sl::OBJECT_CLASS::VEHICLE] = src->od_vehicle_conf;
        class_det_conf[sl::OBJECT_CLASS::ANIMAL] = src->od_animal_conf;
        class_det_conf[sl::OBJECT_CLASS::ELECTRONICS] = src->od_electronics_conf;
        class_det_conf[sl::OBJECT_CLASS::BAG] = src->od_bag_conf;
        class_det_conf[sl::OBJECT_CLASS::FRUIT_VEGETABLE] = src->od_fruit_vegetable_conf;
        class_det_conf[sl::OBJECT_CLASS::SPORT] = src->od_sport_conf;
        od_rt_params.object_class_detection_confidence_threshold = class_det_conf;

        ret = src->zed.retrieveObjects(det_objs, od_rt_params, OD_INSTANCE_MODULE_ID);

        if (ret == sl::ERROR_CODE::SUCCESS) {
            if (det_objs.is_new) {
                GST_LOG_OBJECT(src, "OD new data");

                obj_count = det_objs.object_list.size();
                if (obj_count > 255)
                    obj_count = 255;

                GST_LOG_OBJECT(src, "Number of detected objects (clamped): %d", obj_count);

                uint8_t idx = 0;
                for (auto i = det_objs.object_list.begin();
                     i != det_objs.object_list.end() && idx < obj_count; ++i, ++idx) {
                    sl::ObjectData obj = *i;

                    obj_data[idx].skeletons_avail = FALSE;
                    obj_data[idx].id = obj.id;

                    obj_data[idx].label = static_cast<OBJECT_CLASS>(obj.label);
                    obj_data[idx].sublabel = static_cast<OBJECT_SUBCLASS>(obj.sublabel);

                    obj_data[idx].tracking_state =
                        static_cast<OBJECT_TRACKING_STATE>(obj.tracking_state);
                    obj_data[idx].action_state = static_cast<OBJECT_ACTION_STATE>(obj.action_state);

                    obj_data[idx].confidence = obj.confidence;

                    memcpy(obj_data[idx].position, (void *) obj.position.ptr(), 3 * sizeof(float));
                    memcpy(obj_data[idx].position_covariance, (void *) obj.position_covariance,
                           6 * sizeof(float));
                    memcpy(obj_data[idx].velocity, (void *) obj.velocity.ptr(), 3 * sizeof(float));

                    if (obj.bounding_box_2d.size() > 0) {
                        memcpy((uint8_t *) obj_data[idx].bounding_box_2d,
                               (uint8_t *) obj.bounding_box_2d.data(),
                               obj.bounding_box_2d.size() * 2 * sizeof(unsigned int));
                    }
                    if (obj.bounding_box.size() > 0) {
                        memcpy(obj_data[idx].bounding_box_3d, (void *) obj.bounding_box.data(),
                               24 * sizeof(float));
                    }

                    memcpy(obj_data[idx].dimensions, (void *) obj.dimensions.ptr(),
                           3 * sizeof(float));
                }
            } else {
                obj_count = 0;
            }
        } else {
            GST_WARNING_OBJECT(src, "Object detection problem: '%s' - %s",
                               sl::toString(ret).c_str(), sl::toVerbose(ret).c_str());
        }
    }
    // <---- Object detection metadata

    // ----> Body Tracking metadata
    if (src->body_tracking) {
        guint8 b_idx = obj_count;

        GST_LOG_OBJECT(src, "Body Tracking enabled");

        bt_rt_params.detection_confidence_threshold = src->bt_rt_det_conf;
        bt_rt_params.minimum_keypoints_threshold = src->bt_rt_min_kp_thresh;
        bt_rt_params.skeleton_smoothing = src->bt_rt_skel_smoothing;

        ret = src->zed.retrieveBodies(bodies, bt_rt_params, BT_INSTANCE_MODULE_ID);

        if (ret == sl::ERROR_CODE::SUCCESS) {
            if (bodies.is_new) {
                GST_LOG_OBJECT(src, "BT new data");

                int bodies_count = bodies.body_list.size();
                GST_LOG_OBJECT(src, "Number of detected bodies: %d", bodies_count);

                for (auto i = bodies.body_list.begin(); i != bodies.body_list.end() && b_idx < 256;
                     ++i, ++b_idx) {
                    sl::BodyData obj = *i;

                    obj_data[b_idx].skeletons_avail = TRUE;
                    obj_data[b_idx].id = obj.id;
                    obj_data[b_idx].label = OBJECT_CLASS::PERSON;
                    obj_data[b_idx].sublabel = OBJECT_SUBCLASS::PERSON;

                    obj_data[b_idx].tracking_state =
                        static_cast<OBJECT_TRACKING_STATE>(obj.tracking_state);
                    obj_data[b_idx].action_state =
                        static_cast<OBJECT_ACTION_STATE>(obj.action_state);

                    obj_data[b_idx].confidence = obj.confidence;

                    memcpy(obj_data[b_idx].position, (void *) obj.position.ptr(),
                           3 * sizeof(float));
                    memcpy(obj_data[b_idx].position_covariance, (void *) obj.position_covariance,
                           6 * sizeof(float));
                    memcpy(obj_data[b_idx].velocity, (void *) obj.velocity.ptr(),
                           3 * sizeof(float));

                    if (obj.bounding_box_2d.size() > 0) {
                        memcpy((uint8_t *) obj_data[b_idx].bounding_box_2d,
                               (uint8_t *) obj.bounding_box_2d.data(),
                               obj.bounding_box_2d.size() * 2 * sizeof(unsigned int));
                    }
                    if (obj.bounding_box.size() > 0) {
                        memcpy(obj_data[b_idx].bounding_box_3d, (void *) obj.bounding_box.data(),
                               24 * sizeof(float));
                    }

                    memcpy(obj_data[b_idx].dimensions, (void *) obj.dimensions.ptr(),
                           3 * sizeof(float));

                    switch (static_cast<sl::BODY_FORMAT>(src->bt_format)) {
                    case sl::BODY_FORMAT::BODY_18:
                        obj_data[b_idx].skel_format = 18;
                        break;
                    case sl::BODY_FORMAT::BODY_34:
                        obj_data[b_idx].skel_format = 34;
                        break;
                    case sl::BODY_FORMAT::BODY_38:
                        obj_data[b_idx].skel_format = 38;
                        break;
                    default:
                        obj_data[b_idx].skel_format = 0;
                        break;
                    }

                    if (obj.keypoint_2d.size() > 0 && obj_data[b_idx].skel_format > 0) {
                        memcpy(obj_data[b_idx].keypoint_2d, (void *) obj.keypoint_2d.data(),
                               2 * obj_data[b_idx].skel_format * sizeof(float));
                    }
                    if (obj.keypoint.size() > 0 && obj_data[b_idx].skel_format > 0) {
                        memcpy(obj_data[b_idx].keypoint_3d, (void *) obj.keypoint.data(),
                               3 * obj_data[b_idx].skel_format * sizeof(float));
                    }

                    if (obj.head_bounding_box_2d.size() > 0) {
                        memcpy(obj_data[b_idx].head_bounding_box_2d,
                               (void *) obj.head_bounding_box_2d.data(), 8 * sizeof(unsigned int));
                    }
                    if (obj.head_bounding_box.size() > 0) {
                        memcpy(obj_data[b_idx].head_bounding_box_3d,
                               (void *) obj.head_bounding_box.data(), 24 * sizeof(float));
                    }
                    memcpy(obj_data[b_idx].head_position, (void *) obj.head_position.ptr(),
                           3 * sizeof(float));
                }

                obj_count = b_idx;
            }
        } else {
            GST_WARNING_OBJECT(src, "Body Tracking problem: '%s' - %s", sl::toString(ret).c_str(),
                               sl::toVerbose(ret).c_str());
        }
    }
    // <---- Body Tracking metadata

    // ----> Timestamp meta-data
    GST_BUFFER_TIMESTAMP(buf) =
        GST_CLOCK_DIFF(gst_element_get_base_time(GST_ELEMENT(src)), clock_time);
    GST_BUFFER_DTS(buf) = GST_BUFFER_TIMESTAMP(buf);
    GST_BUFFER_OFFSET(buf) = src->buffer_index++;
    // <---- Timestamp meta-data

    offset = GST_BUFFER_OFFSET(buf);
    gst_buffer_add_zed_src_meta(buf, info, pose, sens,
                                       src->object_detection | src->body_tracking, obj_count,
                                       obj_data, offset);
}
