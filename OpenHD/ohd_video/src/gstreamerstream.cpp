

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <stdexcept>
#include <vector>

#include <regex>

#include <gst/gst.h>

#include <fmt/core.h>

#include "openhd-log.hpp"

#include "OHDGstHelper.hpp"
#include "gstreamerstream.h"



GStreamerStream::GStreamerStream(PlatformType platform,
                                 Camera &camera, 
                                 uint16_t video_udp_port)
    : CameraStream(platform, camera, video_udp_port) {
    std::cerr << "GStreamerStream::GStreamerStream()" << std::endl;
}


void GStreamerStream::setup() {
    std::cerr << "GStreamerStream::setup()" << std::endl;
    GError* error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error)) {
        std::cerr << "gst_init_check() failed: " << error->message << std::endl;
        g_error_free(error);
        throw std::runtime_error("GStreamer initialization failed");
    }
    std::cerr << "Creating GStreamer pipeline" << std::endl;
    // sanity checks
    if(m_camera.bitrateKBits<=100 || m_camera.bitrateKBits>(1024*1024*50)){
        m_camera.bitrateKBits=5000;
    }
    assert(m_camera.userSelectedVideoFormat.isValid());
    assert(m_camera.type!=CameraTypeDummy);
    switch (m_camera.type) {
        case CameraTypeRaspberryPiCSI: {
            setup_raspberrypi_csi();
            break;
        }
        case CameraTypeJetsonCSI: {
            setup_jetson_csi();
            break;
        }
        case CameraTypeUVC: {
            setup_usb_uvc();
            break;
        }
        case CameraTypeV4L2Loopback: {
            setup_usb_uvc();
            break;
        }
        case CameraTypeUVCH264: {
            setup_usb_uvch264();
            break;
        }
        case CameraTypeIP: {
            setup_ip_camera();
            break;
        }
        case CameraTypeUnknown: {
            std::cerr << "Unknown camera type" << std::endl;
            return;
        }
    }
    m_pipeline<< OHDGstHelper::createRtpForVideoCodec(m_camera.userSelectedVideoFormat.videoCodec);
    // Allows users to fully write a manual pipeline, this must be used carefully.
    if (!m_camera.manual_pipeline.empty()) {
        m_pipeline.str("");
        m_pipeline << m_camera.manual_pipeline;
    }
    m_pipeline << OHDGstHelper::createOutputUdpLocalhost(m_video_udp_port);
    // TODO: re-add recording, we need a better way than this crap
    //m_pipeline << "queue ! ";
    // this directs the video stream back to this system for recording in the Record class
    //m_pipeline << fmt::format("udpsink host=127.0.0.1 port={}", m_video_port - 10);
    std::cerr << "Pipeline: " << m_pipeline.str() << std::endl;
    gst_pipeline = gst_parse_launch(m_pipeline.str().c_str(), &error);
    if (error) {
        std::cerr << "Failed to create pipeline: " << error->message << std::endl;
        return;
    }
}


/*
 * This is used to pick a default based on the hardware format
 */
[[maybe_unused]] std::string GStreamerStream::find_v4l2_format(CameraEndpoint &endpoint, bool force_pixel_format, const std::string& pixel_format) {
    std::cerr << "find_v4l2_format" << std::endl;
    std::string width = "1280";
    std::string height = "720";
    std::string fps = "30";
    std::vector<std::string> search_order = {
        "1920x1080@60",
        "1920x1080@30",
        "1280x720@60",
        "1280x720@30",
        "800x600@30",
        "640x480@30",
        "320x240@30"
    };
    for (auto & default_format : search_order) {
        for (auto & format : endpoint.formats) {
            std::smatch result;
            std::regex reg{ "([\\w\\d\\s\\-\\:\\/]*)\\|(\\d*)x(\\d*)\\@(\\d*)"};
            if (std::regex_search(format, result, reg)) {
                std::cerr << "format:"<< format << std::endl;
                if (result.size() == 5) {
                    auto c = fmt::format("{}x{}@{}", width, height, fps);

                    if (force_pixel_format) {
                        if (result[1] == pixel_format && c == default_format) {
                            width = result[2];
                            height = result[3];
                            fps = result[4];
                            
                            return fmt::format("{}x{}@{}", width, height, fps);
                        }
                    } else if (c == default_format) {
                        width = result[2];
                        height = result[3];
                        fps = result[4];

                        return fmt::format("{}x{}@{}", width, height, fps);
                    }
                }
                std::cerr << "unexpected match size"<< result.size() << std::endl;
            }
        }
    }
    // fallback using the default above
    std::cerr << "returning default format:"<< width << " " << height << " " << fps << std::endl;
    return fmt::format("{}x{}@{}", width, height, fps);
}


void GStreamerStream::setup_raspberrypi_csi() {
    std::cerr << "Setting up Raspberry Pi CSI camera" << std::endl;
    assert(m_camera.userSelectedVideoFormat.isValid());
    m_pipeline<<OHDGstHelper::createRpicamsrcStream(m_camera.bus,m_camera.bitrateKBits,m_camera.userSelectedVideoFormat);
}


void GStreamerStream::setup_jetson_csi() {
    std::cerr << "Setting up Jetson CSI camera" << std::endl;
    // if there's no endpoint this isn't a runtime bug but a programming error in the system service,
    // because it never found an endpoint to use for some reason
    // TODO well, we should not have to deal with that here ?!
    auto endpoint = m_camera.endpoints.front();
    int sensor_id = -1;
    std::smatch result;
    std::regex reg{ "/dev/video([\\d])"};
    if (std::regex_search(endpoint.device_node, result, reg)) {
        if (result.size() == 2) {
            std::string s = result[1];
            sensor_id = std::stoi(s);
        }
    }
    if (sensor_id == -1) {
        ohd_log(STATUS_LEVEL_CRITICAL, "Failed to determine Jetson CSI sensor ID");
        return;
    }
    m_pipeline<<OHDGstHelper::createJetsonStream(sensor_id,m_camera.bitrateKBits,m_camera.userSelectedVideoFormat);
}

void GStreamerStream::setup_usb_uvc() {
    std::cerr << "Setting up usb UVC camera" << std::endl;
    std::string device_node;
    std::cerr << m_camera.name << " type " << m_camera.type << std::endl;
    for (auto &endpoint : m_camera.endpoints) {
        if (m_camera.userSelectedVideoFormat.videoCodec == VideoCodecH264 && endpoint.support_h264) {
           std::cerr << "h264" << std::endl;
            device_node = endpoint.device_node;
            m_pipeline << fmt::format("v4l2src name=picturectrl device={} ! ", device_node);
            const auto videoFormat=m_camera.userSelectedVideoFormat;
            m_pipeline << fmt::format("video/x-h264, width={}, height={}, framerate={}/1 ! ",
                                      videoFormat.width, videoFormat.height,videoFormat.framerate);
            break;
        }
        if (m_camera.userSelectedVideoFormat.videoCodec == VideoCodecMJPEG && endpoint.support_mjpeg) {
            std::cerr << "MJPEG" << std::endl;
            device_node = endpoint.device_node;
            m_pipeline << fmt::format("v4l2src name=picturectrl device={} ! ", device_node);
            const auto videoFormat=m_camera.userSelectedVideoFormat;
            m_pipeline << fmt::format("image/jpeg, width={}, height={}, framerate={}/1 ! ",
                                      videoFormat.width, videoFormat.height,videoFormat.framerate);
            break;
        }
    }
    /*
     * No H264 or MJPEG endpoint was found/chosen, so we do YUV encoding. Most of these will be thermal cameras and 
     * won't be handled like this very long because support for them will be in a different class with thermal 
     * span and pallete support. However once in a while people do connect YUV webcams for testing purposes, and this
     * code supports those too.
     */
    if (device_node.empty()) {
        for (auto &endpoint : m_camera.endpoints) {
            std::cerr << "empty" << std::endl;
            if (endpoint.support_raw) {
                device_node = endpoint.device_node;
                m_pipeline << fmt::format("v4l2src name=picturectrl device={} ! ", device_node);
                std::cerr << "Allowing gstreamer to choose UVC format" << std::endl;
                m_pipeline << fmt::format("video/x-raw ! ");

                m_pipeline << "videoconvert ! ";

                if (m_camera.userSelectedVideoFormat.videoCodec== VideoCodecH265) {
                    m_pipeline << fmt::format("x265enc name=encodectrl bitrate={} ! ", m_camera.bitrateKBits);
                } else {
                    m_pipeline << fmt::format("x264enc name=encodectrl bitrate={} tune=zerolatency key-int-max=10 ! ", m_camera.bitrateKBits);
                }

            }
        }
    }
}

void GStreamerStream::setup_usb_uvch264() {
    std::cerr << "Setting up UVC H264 camera" << std::endl;
    const auto endpoint=m_camera.endpoints.front();
    // uvch265 cameras don't seem to exist, codec setting is ignored
    m_pipeline<<OHDGstHelper::createUVCH264Stream(endpoint.device_node,m_camera.bitrateKBits,m_camera.userSelectedVideoFormat);
}


void GStreamerStream::setup_ip_camera() {
    std::cerr << "Setting up IP camera" << std::endl;
    if (m_camera.url.empty()) {
        m_camera.url = "rtsp://192.168.0.10:554/user=admin&password=&channel=1&stream=0.sdp";
    }
    m_pipeline<<OHDGstHelper::createIpCameraStream(m_camera.url);
}

std::string GStreamerStream::debug() {
    std::cerr << "GS_debug";
    GstState state;
    GstState pending;
    auto returnValue = gst_element_get_state(gst_pipeline, &state ,&pending, 1000000000);
    std::cerr << "Gst state:" << returnValue << "." << state << "."<< pending << "." << std::endl;
    if (returnValue==0){
        stop();
        sleep(3);
        start();
    }
    return {};
}

void GStreamerStream::start() {
    std::cerr << "GStreamerStream::start()" << std::endl;
    gst_element_set_state(gst_pipeline, GST_STATE_PLAYING);
    GstState state;
    GstState pending;
    auto returnValue = gst_element_get_state(gst_pipeline, &state ,&pending, 1000000000);
    std::cerr << "Gst state:" << returnValue << "." << state << "."<< pending << "." << std::endl;
}


void GStreamerStream::stop() {
    std::cerr << "GStreamerStream::stop()" << std::endl;
    gst_element_set_state(gst_pipeline, GST_STATE_PAUSED);
}


bool GStreamerStream::supports_bitrate() {
    std::cerr << "GStreamerStream::supports_bitrate()" << std::endl;
    return false;
}

void GStreamerStream::set_bitrate(int bitrate) {
    std::cerr << "Unmplemented GStreamerStream::set_bitrate(" << bitrate << ")" << std::endl;
}

bool GStreamerStream::supports_cbr() {
    std::cerr << "GStreamerStream::supports_cbr()" << std::endl;
    return false;
}

void GStreamerStream::set_cbr(bool enable) {
    std::cerr << "Unsupported GStreamerStream::set_cbr(" << enable << ")" << std::endl;
}


VideoFormat GStreamerStream::get_format() {
    std::cerr << "GStreamerStream::get_format()" << std::endl;
    return m_camera.userSelectedVideoFormat;
}


void GStreamerStream::set_format(VideoFormat videoFormat) {
    std::cerr << "GStreamerStream::set_format(" << videoFormat.toString() << ")" << std::endl;
    m_camera.userSelectedVideoFormat=videoFormat;
}






