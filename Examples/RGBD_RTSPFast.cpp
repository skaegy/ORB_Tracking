//
// Created by root on 07/09/18.
//

//
// Created by root on 30/08/18.
//

//
// Created by skaegy on 07/08/18.
//

#include<iostream>
#include <vector>
#include <list>
#include <thread>
#include <algorithm>
#include <fstream>
#include <omp.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include "System.h"

using namespace std;

int main()
{
    // ==================== Load settings  ==================== //
    const string &strSettingPath = "../Setting.yaml";


    cv::FileStorage fSettings(strSettingPath, cv::FileStorage::READ);
    if(!fSettings.isOpened())
    {
        cerr << "Failed to open setting file at: " << strSettingPath << endl;
        exit(-1);
    }
    const string videoSource = fSettings["Video_source"];
    const string strORBvoc = fSettings["Orb_Vocabulary"];
    const string strCamSet = fSettings["Cam_Setting"];
    int ReuseMap = fSettings["is_ReuseMap"];
    const string strMapPath = fSettings["ReuseMap"];

    const string strArucoParamsFile = fSettings["Aruco_Parameters"];
    int ArucoDetect = fSettings["is_DetectMarker"];
    const string strOpenposeSettingFile = fSettings["Openpose_Parameters"];
    int HumanPose = fSettings["is_DetectHuman"];
    fSettings.release();

    // =================== Load camera parameters =================== //
    cv::FileStorage fs(strCamSet, cv::FileStorage::READ);
    const int IMG_WIDTH = fs["Camera.width"];
    const int IMG_HEIGHT = fs["Camera.height"];
    const int IN_FRAME = fs["Camera.fps"];


    bool bReuseMap = false;
    if (1 == ReuseMap)
        bReuseMap = true;
    bool bHumanPose = false;
    if (1 == HumanPose)
        bHumanPose = true;
    bool bArucoDetect = false;
    if (1 == ArucoDetect)
        bArucoDetect = true;

    // Create SLAM system. It initializes all system threads and gets ready to process frames.
    ORB_SLAM2::System SLAM(strORBvoc, strCamSet, strArucoParamsFile, strOpenposeSettingFile,
                           ORB_SLAM2::System::MONOCULAR, true, bReuseMap, bHumanPose, bArucoDetect, strMapPath);

    cout << endl << "-------" << endl;
    cout << "Start processing sequence ..." << endl;

    // Main loop
    bool OpStandBy, ARUCOStandBy;
    cv::Mat imAruco, imOP;
    list<cv::Mat> processed_color;
    list<cv::Mat> processed_depth;
    cv::VideoCapture capture(videoSource);

    std::thread LoadRealsense([&]() {
        while(1){
            if (capture.isOpened()){
                // Read image from RTSP streaming (imCombine is encoded)
                cv::Mat imCombine;
                std::vector<cv::Mat> channel(3);
                capture >> imCombine;

                // ========== Convert Depth (CV_8UC3) to (CV_16UC1) =========== //
                cv::Mat imRGB(cv::Size(IMG_WIDTH, IMG_HEIGHT), CV_8UC3);
                cv::Mat imD(cv::Size(IMG_WIDTH, IMG_HEIGHT), CV_16UC1);
                cv::Mat imD_C3(cv::Size(IMG_WIDTH, IMG_HEIGHT), CV_8UC3);
                // Copy color image and depth(colormap) image
                imCombine.rowRange(0,IMG_HEIGHT).copyTo(imRGB);
                imCombine.rowRange(IMG_HEIGHT,IMG_HEIGHT*2).copyTo(imD_C3);

                //cv::Mat imD_C3_smooth;
                cv::medianBlur(imD_C3, imD_C3, 5);
                std::vector<cv::Mat> channels(3);
                cv::split(imD_C3, channels);
                /// Decoding
                channels[0].setTo(cv::Scalar(0), channels[0] < 20);
                channels[1].setTo(cv::Scalar(0), channels[0] < 20);
                channels[2].setTo(cv::Scalar(0), channels[0] < 20);
                channels[0].setTo(cv::Scalar(0), channels[1] < 20);
                channels[1].setTo(cv::Scalar(0), channels[1] < 20);
                channels[2].setTo(cv::Scalar(0), channels[1] < 20);
                channels[0].setTo(cv::Scalar(0), channels[2] < 20);
                channels[1].setTo(cv::Scalar(0), channels[2] < 20);
                channels[2].setTo(cv::Scalar(0), channels[2] < 20);

                channels[0].setTo(cv::Scalar(0), channels[0] > 240);
                channels[1].setTo(cv::Scalar(0), channels[0] > 240);
                channels[2].setTo(cv::Scalar(0), channels[0] > 240);
                channels[0].setTo(cv::Scalar(0), channels[1] > 240);
                channels[1].setTo(cv::Scalar(0), channels[1] > 240);
                channels[2].setTo(cv::Scalar(0), channels[1] > 240);
                channels[0].setTo(cv::Scalar(0), channels[2] > 240);
                channels[1].setTo(cv::Scalar(0), channels[2] > 240);
                channels[2].setTo(cv::Scalar(0), channels[2] > 240);

                channels[0].convertTo(channels[0], CV_16U, 16.0);
                channels[1].convertTo(channels[1], CV_16U, 16.0);
                channels[2].convertTo(channels[2], CV_16U, 16.0);
                imD = channels[0] + channels[1] + channels[2];
                imD.convertTo(imD, CV_16U, 1.0/3.0);

                //cv::Mat imD_smooth;
                cv::medianBlur(imD, imD, 5);


                // Store processed RGB & Depth in the std::list
                processed_color.push_front(imRGB);
                processed_depth.push_front(imD);
                if (processed_color.size() > 2)
                    processed_color.pop_back();
                if (processed_depth.size() > 2)
                    processed_depth.pop_back();
            }
            else{
                cerr << "Can not open streaming video. Please check again!" << endl;
                break;
            }
        }
    });
    LoadRealsense.detach();


    while(1) {
        if (processed_depth.size() > 0 && processed_color.size() > 0) {
            if (bHumanPose)
                OpStandBy = SLAM.mpOpDetector->OpStandBy;
            if (bArucoDetect)
                ARUCOStandBy = SLAM.mpArucoDetector->ArucoStandBy;

            std::chrono::milliseconds unix_timestamp = std::chrono::duration_cast< std::chrono::milliseconds >
                    (std::chrono::system_clock::now().time_since_epoch());
            double unix_timestamp_ms = std::chrono::milliseconds(unix_timestamp).count();

            // Read image from realsense
            cv::Mat imD = processed_depth.front();
            cv::Mat imRGB = processed_color.front();

            // Pass the image to the SLAM system
            SLAM.TrackMonocular(imRGB, unix_timestamp_ms);

            // Pass the image to ARUCO marker detection system
            imRGB.copyTo(imAruco);
            if (ARUCOStandBy)
                SLAM.mpArucoDetector->ArucoLoadImage(imAruco, unix_timestamp_ms);

            // Pass the image to Openpose system
            imRGB.copyTo(imOP);
            if (OpStandBy)
                SLAM.mpOpDetector->OpLoadImageRGBD(imOP, imD, unix_timestamp_ms);
        }

        if (SLAM.isShutdown())
            break;
    }
    // Stop all threads
    // SLAM.Shutdown();

    // Save camera trajectory
    SLAM.SaveKeyFrameTrajectory("RGBDTrajectory.txt");

    SLAM.SaveSkeletonTrajectory("HumanSkeletonTrajectory.txt");

    return 0;
}
