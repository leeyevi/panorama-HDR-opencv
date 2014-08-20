#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "NativeLogging.h"

static const char* TAG = "CreatePano";

using namespace std;
using namespace cv;

void show_image(Mat &image) {
	if (!image.data) {
		cout << "Image is missing\n";
		exit (-1);
	}
	cout << "Image size: " << image.rows << " x " << image.cols << "\n";
	namedWindow( "Display window", WINDOW_AUTOSIZE );
	imshow("Display window", image);
	waitKey();

}

void find_correspondences(vector<Mat> inputImages, vector<vector<Point2f>>& imgMatches, string outputImagePath, bool writeOutput)
{
    // Extract features from each picture with your favorite detector.
	ORB detector (2000);
    vector<Mat> descriptors(inputImages.size());
    vector<vector<KeyPoint>> keypoints(inputImages.size());
    BFMatcher matcher (NORM_HAMMING, false);

	if (inputImages.size() < 2) exit(-1);

	detector(inputImages[0], Mat(), keypoints[0],descriptors[0]);
    for(int i = 1; i < inputImages.size(); i++) {
		detector(inputImages[i], Mat(), keypoints[i],descriptors[i]);

		// Match the features.
        vector<DMatch> matches;
        matcher.match(descriptors[i], descriptors[i-1], matches);
		
		// Prune 'bad' matches based on some metric.
		// you can do it by using DMatch.distance() and choose the one with the best distance to get the best results.
		// huristic: any distance taht is < 3x the min, we consider good and 
		// getting around 10-15 matches is enough
		
		// Find the point correspondences.
		float minMatchDistance = 1e9;
		for (auto &match : matches) {
			if (match.distance < minMatchDistance)
				minMatchDistance = match.distance;
		}
		for (auto &match : matches) {
			if (match.distance < 3 * minMatchDistance) {
				imgMatches[i-1].push_back(keypoints[i-1][match.trainIdx].pt);
				imgMatches[i].push_back(keypoints[i][match.queryIdx].pt);
			}
		}
	}
    /* Visualize and report your 'good_matches' (if enabled). */
    if(writeOutput)
    {
      Mat intermediateImage;
      // Draw your matches here
      imwrite(outputImagePath, intermediateImage);
    }
}

int min(int x, int y, int z) {
	if(x <= y) {
		return (x < z)?x:z;
	} else {
		return (y < z)?y:z;
	}
}

int max(int x, int y, int z) {
	if(x >= y) {
		return (x > z)?x:z;
	} else {
		return (y > z)?y:z;
	}
}

int min2(int x, int y) {
	return (x < y)?x:y;
}

int max2(int x, int y) {
	return (x > y)?x:y;
}

void apply_homography(vector<Mat>& inputImages, vector<vector<Point2f>>& matches, vector<Mat> &warpedImages)
{

    /* **************************************************************
     * If you got this far, congrats!! You are half way there. Now for the second portion.
     * *************************************************************** */

    // Re-comment out the drawMatches block.

    // try to guess the left and right images
    // from the average of the x coordinates of the matches vector
    // we can only proceed if index 0 = left and index 1 = right
    // else we swap the two and then proceed
    float sumX[2];
    for(int i = 0; i < 2; i++) {
        for(auto &pt : matches[i]) {
            sumX[i] += pt.x;
        }
    }

    if(sumX[0] < sumX[1]) {
        // we have to swap the inputImages and the matches
        Mat temp = inputImages[0];
        inputImages[0] = inputImages[1];
        inputImages[1] = temp;

        vector<Point2f> ptemp(matches[0]);
        matches[0] = matches[1];
        matches[1] = ptemp;
    }
    // Compute the homography via RANSAC.
    Mat H12 = findHomography(matches[1], matches[0], CV_RANSAC, 10);

    // Compute the corner corrospondences.
	vector<Vec2f> corners(4), persXform(4);
	corners[0] = Point2f(0,0);
	corners[1] = Point2f(0,inputImages[1].rows-1);
	corners[2] = Point2f(inputImages[1].cols-1,0);
	corners[3] = Point2f(inputImages[1].cols-1,inputImages[1].rows-1);
	perspectiveTransform(corners, persXform, H12);

	int boundaryLow, boundaryHigh;
	int boundaryRight, boundaryLeft=0;

	boundaryHigh = min(inputImages[0].rows-1, persXform[1][1], persXform[3][1]);
	boundaryLow = max(0, persXform[0][1], persXform[2][1]);

	boundaryLeft = min2(0, max2(persXform[0][0], persXform[1][0]));
	boundaryRight = max2(inputImages[0].cols-1, min2(persXform[2][0], persXform[3][0]));

    int bbLow = min(0, persXform[0][1], persXform[2][1]);
    int bbHigh = max(inputImages[0].rows-1, persXform[1][1], persXform[3][1]);

    int bbLeft = min(0, persXform[0][0], persXform[1][0]);
    int bbRight = max(inputImages[0].cols-1, persXform[2][0], persXform[3][0]);

	int sizeX = bbRight - bbLeft;
	int sizeY = bbHigh - bbLow;

	Size wholeImage(sizeX, sizeY);
    // adjust for the case where the idx 0 image is on the right
    int left = 0, right = 0;
    right = boundaryRight - inputImages[0].cols;

    copyMakeBorder(inputImages[0], warpedImages[0], 0, 0, left, right,  BORDER_CONSTANT);
    warpPerspective(inputImages[1], warpedImages[1], H12, wholeImage);

	Rect myROI(0, boundaryLow, boundaryRight-boundaryLeft, boundaryHigh-boundaryLow);

	warpedImages[0] = warpedImages[0](myROI);
	warpedImages[1] = warpedImages[1](myROI);
    // Create the left and right templates (warpPerspective might be useful here).
}

float takeMin (float left, float mid, float right, int &indx) {
	if (left <= mid && left <= right) {indx = -1; return left;}
	else if (mid <= left && mid <= right) {indx = 0; return mid;}
	else if (right <= left && right <= mid) {indx = 1; return right;}
}

void compute_weights(vector<Mat>& images, vector<Mat>& weights) {
	weights[0] = Mat(images[0].rows, images[0].cols, CV_32F);
	weights[1] = Mat(images[0].rows, images[0].cols, CV_32F);
	vector<int> seamColsIndx (images[0].rows);
	vector<vector<float>> costMat(images[0].rows, vector<float>(images[0].cols, 1e9));
	vector<vector<int>> backTrackMat(images[0].rows, vector<int>(images[0].cols, 0));
	for (int i = 0; i < images[0].rows; i++) {
		for (int j = 0; j < images[0].cols; j++) {
			float me = norm(images[0].at<Vec3f>(i,j) - images[1].at<Vec3f>(i,j));
			if (i == 0) {
				costMat[i][j] = me;
			} else if ( j == 0 ) {
				float top = costMat[i-1][j];
				float topRight = costMat[i-1][j+1];
				costMat[i][j] = takeMin(1e9, top, topRight, backTrackMat[i][j]) + me;
			} else if ( j == images[0].cols-1 ) {
				float top = costMat[i-1][j];
				float topLeft = costMat[i-1][j-1];
				costMat[i][j] = takeMin(topLeft, top, 1e9, backTrackMat[i][j]) + me;
			} else {
				float top = costMat[i-1][j];
				float topLeft = costMat[i-1][j-1];
				float topRight = costMat[i-1][j+1];
				costMat[i][j] = takeMin(topLeft, top, topRight, backTrackMat[i][j]) + me;
			}
		}
	}

	float minVal = 1e9;
	int minCol = -1;
	for (int j = 0; j < images[0].cols; j++) {
		if (costMat[images[0].rows-1][j] < minVal) {
			minVal = costMat[images[0].rows-1][j];
			minCol = j;
		}
	}

	int seamCol = minCol;
	// we need to start at the bottom row and go up
	// based on the backTrackMat
	for (int i = images[0].rows-1; i >= 0; i--) {
		for (int j = 0; j < images[0].cols; j++) {
			if (j < seamCol) {
				weights[0].at<float>(i,j) = 1;
				weights[1].at<float>(i,j) = 0;
			} else {
				weights[0].at<float>(i,j) = 0;
				weights[1].at<float>(i,j) = 1;
			}
		}
		seamCol += backTrackMat[i][seamCol];
	}
}

void get_gaussian_pyr(int numLevels, Mat &inMat, vector<Mat> &gaussPyr) {
	buildPyramid(inMat,gaussPyr,numLevels);
}

void get_laplacian_pyr(int numLevels, Mat &imgIn, vector<Mat> &laplacePyr) {
	cout << "laplacian in \n";
	get_gaussian_pyr(numLevels-1, imgIn, laplacePyr);
	for (int i = 1; i < numLevels; i++) {
		Mat pyrUpImg;
		pyrUp(laplacePyr[i], pyrUpImg);
		resize(pyrUpImg, pyrUpImg, laplacePyr[i-1].size());
		subtract(laplacePyr[i-1], pyrUpImg, laplacePyr[i-1]);
	}
	cout << "laplacian out \n";
}

void collapse_laplacian_pyr(int numLevels, vector<Mat> &laplacePyr, Mat &imgOut) {
	imgOut = laplacePyr[laplacePyr.size()-1];
	for (int i = numLevels - 2; i >= 0; i--) {
		Mat pyrUpImg;
		pyrUp(imgOut, pyrUpImg);
		resize(pyrUpImg, pyrUpImg, laplacePyr[i].size());
		add(laplacePyr[i], pyrUpImg, imgOut);
	}
}

void convert_img_to_char(Mat &img) {
	Mat img_new(img.rows,img.cols,CV_8UC3);
	for (int r = 0; r < img.rows; r++) {
		for (int c = 0; c < img.cols; c++) {
			for (int k = 0; k < 3; k++) {
				img_new.at<Vec3b>(r,c)[k] = saturate_cast<uchar>(img.at<Vec3f>(r,c)[k]*255.0);
			}
		}
	}
	img = img_new;
}

void convert_img_to_float(vector<Mat> &images) {
	//Convert image pixels from unsigned char to float
	for (auto &img : images) {
		Mat img_new(img.rows,img.cols,CV_32FC3);
		for (int r = 0; r < img.rows; r++) {
			for (int c = 0; c < img.cols; c++) {
				for (int k = 0; k < 3; k++) {
					img_new.at<Vec3f>(r,c)[k] = img.at<Vec3b>(r,c)[k]/255.0;
				}
			}
		}
		img = img_new;
	}
}

Mat blend_images_simple(vector<Mat>& inputImages, vector<Mat>& warpedImages) {
	show_image(warpedImages[0]);
	show_image(warpedImages[1]);
	convert_img_to_float(warpedImages);
    // Create the blending weight matrix, make sure the white portion is the right size! This matters!
    vector<Mat> weights(2);
    compute_weights(warpedImages, weights);
	Mat output(weights[0].rows, weights[0].cols, CV_32FC3);
	for (int i = 0; i < weights.size(); i++) {
		for(int j = 0; j < weights[0].rows; j++) {
			for(int k = 0; k < weights[0].cols; k++) {
				output.at<Vec3f>(j,k) = output.at<Vec3f>(j,k) +
					weights[i].at<float>(j,k) * warpedImages[i].at<Vec3f>(j,k);
			}
		}
	}
	for (auto &weight : weights) {
		show_image(weight);
	}

	show_image(output);
	convert_img_to_char(output);
    return output;
	
}

bool CreatePanorama(vector<string> inputImagePaths, string outputImagePath)
{

    /*  We have broken this development into two stages.
     *
     *  The steps in the first portion are as follows:
     *   1) Extract features from each picture (Helpful OpenCV functions: OrbFeatureDetector)
     *   2) Match the features                 (Helpful OpenCV functions: BFMatcher)
     *   3) Prune the matches                  (Helpful OpenCV functions: DMatch)
     *   4) Output the drawMatches             (Helpful OpenCV functions: drawMatches)

     *  The steps in the second portion are as follows:
     *   1) Compute the homography via RANSAC  (Helpful OpenCV functions: findHomography, perspectiveTransform)
     *   2) Create the left and right templates(Helpful OpenCV functions: Mat())
     *   3) Create the blendingWeights         (Helpful OpenCV functions: Range::all())
     *   4) Implement Laplacian blending       (Helpful OpenCV functions: pyrUp/pyrDown)
     */
    vector<Mat> inputImages;
    for(string imgPath : inputImagePaths)
    {
        Mat img = imread(imgPath);
        inputImages.push_back(img);
    }

    // Set this to true if you want to visualize your intermediate matches.
    bool saveIntermediate = false;
    vector<vector<Point2f>> imgMatches(2);
    find_correspondences(inputImages, imgMatches, outputImagePath, saveIntermediate);
    if(saveIntermediate)
    {
    	return true;
    }

    vector<Mat> warpedImages(2);
    apply_homography(inputImages, imgMatches, warpedImages);

    Mat outputPano = blend_images_simple(inputImages, warpedImages);

    imwrite(outputImagePath, outputPano);

    return true;
}

int main(int argc, char *argv[]) {
	vector<string> imageString;
	for (int i = 1; i < argc-1; i++) {
		imageString.push_back(string(argv[i]));
	}
	CreatePanorama(imageString, argv[argc-1]);
	return 0;
}
