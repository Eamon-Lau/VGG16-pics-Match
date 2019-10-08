// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file bilines.cpp
 * @brief Display bilinear level lines.
 * 
 * (C) 2019, Pascal Monasse <pascal.monasse@enpc.fr>
 */
//readme  i'm ��һ��  ���ú�opencv��// �������� �� ����-��c++ -�� Ԥ������-�� Ԥ���������� ��� _SCL_SECURE_NO_WARNINGS
#include "lltree.h"
#include "draw_curve.h"
#include "fill_curve.h"
#include "libBasic.h"
#include "libDetectionIpol.h"

#include <map>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <sstream>

#include "opencv2/imgproc.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"

#define  M_PI 3.1415926
#define  M_PI_2(x)x*x  


using namespace std;

void save_lym(vector<str_point_descriptor> &cCorners, char *fileCorners, cv::Mat img_grey, char* save_path )
{
	//�����һ�����ݣ���


	//����ڶ������ݣ�
	std::ofstream ocfile;
	ocfile.open(fileCorners);

	for (int kk = 0; kk < (int)cCorners.size(); kk++)
	{

		int px = cCorners[kk].px;
		int py = cCorners[kk].py;
		cv::Point p2;
		p2.x = px;
		p2.y = py;
		cv::circle(img_grey, p2, 3, cv::Scalar(255, 0, 0), -1);
		ocfile << px << " " << py << " " << cCorners[kk].ncorbes << " " << std::endl;
	}

	ocfile.close();
	cv::imwrite(save_path, img_grey);
//	cv::imshow("det_point", img_grey);
	// This function generates the output files.
}

void uchar_float(cv::Mat m_grey, int width, float* input)
{
	int row_corner, col_corner;
	uchar *ptr_corner;//Դָ��

	for (row_corner = 0; row_corner < m_grey.rows; row_corner++)
	{
		ptr_corner = m_grey.data + row_corner* width;
		for (col_corner = 0; col_corner < m_grey.cols; col_corner++)
		{
			input[row_corner * width + col_corner] = (float)*ptr_corner;
			ptr_corner += m_grey.channels();
		}
	}

}

void _pixel_operation1(cv::Mat img)
{
//---------------------����ͨ���ϵ� ����----��Ϊȫ���Ƕ�Mat��ָ����� ����Ҫ����ֵ---------------------------------

	int width = img.cols;
	int height = img.rows;
	int wh = width*height;
	int row, col;
	int w;
	uchar *ptr;//Դָ��

	for (row = 0; row < height; row++)
	{
		ptr = img.data + row* width*img.channels();
		for (col = 0; col < width; col++)
		{
			if (*ptr>150 && *(ptr + 1) > 150 && *(ptr + 2) > 150)
			{
				*ptr = 0;
				*(ptr + 1) = 0;
				*(ptr + 2) = 0;
			}
			else{
				*ptr = 255;
				*(ptr + 1) = 255;
				*(ptr + 2) = 255;
			}
			ptr += img.channels();
		}
	}

}


/// Compute histogram of level at pixels at the border of the image.
static void histogram(unsigned char* im, size_t w, size_t h, size_t histo[256]){
    size_t j;
    for(j=0; j<w; j++) // First line
        ++histo[im[j]];
    for(size_t i=1; i+1<h; i++) { // All lines except first and last
        ++histo[im[j]];  // First pixel of line
        j+= w-1;
        ++histo[im[j++]]; // Last pixel of line
    }
    for(; j<w*h; j++) // Last line
        ++histo[im[j]];    
}

/// Put pixels at border of image to value \a v.
static void put_border(unsigned char* im, size_t w, size_t h, unsigned char v) {
    size_t j;
    for(j=0; j<w; j++)
        im[j] = v;
    for(size_t i=1; i+1<h; i++) {
        im[j] = v;
        j+= w-1;
        im[j++] = v;
    }
    for(; j<w*h; j++)
        im[j] = v;
}

/// Set all pixels at border of image to their median level.
static unsigned char fill_border(unsigned char* im, size_t w, size_t h) {
    size_t histo[256] = {0}; // This puts all values to zero
    histogram(im, w, h, histo);     //ͳ��ÿһ����Ե���ص�ֵ
    size_t limit=w+h-2; // Half number of pixels at border    //��ֵlimit
    size_t sum=0;
    int i=-1;
    while((sum+=histo[++i]) < limit);    //  �൱��ͳ����һ����Ե��ֵ
    put_border(im,w,h, (unsigned char)i);  //Ӧͳ�Ƶ�ֵ���¸�����Ե
    return (unsigned char)i;
}

/// Find upper/lower level sets and shift level accordingly: [1]Algorithm 6.
static void fix_levels(LLTree& tree, unsigned char bg, int qStep) {
    std::map<LLTree::Node*,bool> upper;
    for(LLTree::iterator it=tree.begin(); it!=tree.end(); ++it) {
        float parentLevel = it->parent? it->parent->ll->level: bg;
        bool up = it->ll->level > parentLevel;
        if(it->ll->level == parentLevel)
            up = !upper[it->parent];
        upper[&*it] = up;
    }
    float delta = 0.5f*(float)qStep;
    for(LLTree::iterator it=tree.begin(); it!=tree.end(); ++it) {
        if(upper[&*it])
            it->ll->level = std::min(it->ll->level+delta,255.0f);
        else
            it->ll->level = std::max(it->ll->level-delta,0.0f);
    }
}

/// Return depth of node in tree. Roots are at depth 0.
static int depth(const LLTree::Node& node) {
    const LLTree::Node* n=&node;
    int d=0;
    while(n->parent) {
        n = n->parent;
        ++d;
    }
    return d;
}

/// Palette 'rainbow' of gnuplot, from purple to red through blue and yellow.
static void palette(float x,
                    unsigned char& r, unsigned char& g, unsigned char& b) {
    r = (unsigned char)(255*std::min(1.0f, std::abs(2*x-.5f)));
    g = (unsigned char)(255*std::sin(M_PI*x));
    b = (unsigned char)(255*std::cos(M_PI_2(M_PI)));
}


void main(){
	//����
	char* src_path = "C:/vs2013�Լ�Ϲ��/_contour_corner/utils/����ͼƬ/shang2.jpg";
	char* des_path = "C:/vs2013�Լ�Ϲ��/_contour_corner/utils/result/shang2please.jpg";
	char* save_path = "C:/vs2013�Լ�Ϲ��/_contour_corner/utils/result/shang2save.jpg";
	char *fileCorners = "C:/vs2013�Լ�Ϲ��/_contour_corner/utils/result/aaa.txt";
	int qstep = 32;
    cv::Mat image = cv::imread(src_path);
	//cv::imshow("rgb", image);
	//Ԥ���� ��ֵ�� and �������� ������
	cv::Mat image_grey;   //libUSTG::flimage inputImage = inputImageC.getGray();
	cv::cvtColor(image, image_grey, cv::COLOR_BGR2GRAY);
	cv::threshold(image_grey, image_grey, 150, 255, cv::THRESH_BINARY_INV);
	cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 7));//��֤������
	cv::erode(image_grey, image_grey, kernel);
	cv::dilate(image_grey, image_grey, kernel);
	//����һ�α�����  �������ڲ���ë������һ���γɵĺ�ɫ�ߵ�
	cv::dilate(image_grey, image_grey, kernel);
	cv::erode(image_grey, image_grey, kernel);
	
	size_t w = image.cols;
	size_t h = image.rows;

	std::cout << "rgb_w : " << w << std::endl;
	std::cout << "rgb_h : " << h << std::endl;

	//��̬������������������  ���Ѿ����ڵ�ͨ���������
	unsigned char *in = new unsigned char[w * h]; ////////-------------cvu8��uchar��-------Ҫ��һ������ʦ  uchar �� unsigned char ������ɶ��һ��
	int row, col;
	uchar *ptr;//Դָ��

	for (row = 0; row < image.rows; row++)
	{
		ptr = image_grey.data + row* w;
		for (col = 0; col < image.cols; col++)
		{
			in[row * w + col] = (unsigned char)*ptr;
			ptr += 1;
		}
	}
	//�������������㷨

	if (!in) {
		std::cerr << "Error reading as PNG image: " << std::endl;
	}
	uchar bg = fill_border(in, w, h); 
	//uchar bg = (uchar)255; // Background gray of output   //ֱ����inԭͼ��ָ���Ͻ��б�Ե�Ĳ���  //��ֱ�Ӷ�����˰�ɫ
	std::cout << "bg::" << "  " << bg << std::endl;


	// 1. Extract tree of bilinear level lines
	const float offset = 0.5f;         //����ˮƽ����ʱ����С����ֵ
	LLTree tree(in, (int)w, (int)h, offset, qstep, 0);
	free(in);
	//delete[] in;
	std::cout << tree.nodes().size() << " level lines." << std::endl;
	// 2. Adjust levels according to upper/lower level set
	fix_levels(tree, bg, qstep);
	// 3. Reconstruct quantized image from level lines: [1]Algorithm 5.
	unsigned char* out = new unsigned char[3 * w*h];
	std::fill(out, out + 3 * w*h, bg);
	std::vector< std::vector<float> > inter;
	for (LLTree::iterator it = tree.begin(); it != tree.end(); ++it)
		fill_curve(it->ll->line, (unsigned char)it->ll->level, out, (int)w, (int)h, &inter);
	std::copy(out, out + w*h, out + 1 * w*h); // Copy to green channel
	std::copy(out, out + w*h, out + 2 * w*h); // Copy to red channel
	// 4. Draw level lines with a color depending on their depth in tree
	int depthMax = 0;
	for (LLTree::iterator it = tree.begin(); it != tree.end(); ++it) {
		int d = depth(*it);
		if (depthMax<d)
			depthMax = d;
	}
	std::cout << "Max depth of tree: " << depthMax << std::endl;

	for (LLTree::iterator it = tree.begin(); it != tree.end(); ++it) {
		int d = depth(*it);
		unsigned char r, g, b;
		palette(d / (float)depthMax, r, g, b);
		draw_curve(it->ll->line, r, out + 0 * w*h, (int)w, (int)h);
		draw_curve(it->ll->line, g, out + 1 * w*h, (int)w, (int)h);
		draw_curve(it->ll->line, b, out + 2 * w*h, (int)w, (int)h);
	}

	// Output image b g r // �����ߵ���ͨ�� �����opencv����ͨ��
	uchar* save = new uchar[3*w*h];
	int save_ptr = 0;
	for (int begin = 0; begin < w*h; begin++)
	{

		if (out[begin] && out[begin + w*h] && out[begin + 2 * w*h]){
			save[save_ptr] = out[begin + 2 * w*h];
			save[save_ptr+1] = out[begin + w*h];
			save[save_ptr+2] = out[begin];
		}
		save_ptr += 3;

	}

	cv::Mat m(h, w, CV_8UC3, save);

    //���ҵ�������ת����ֵͼ���� ��ɫ����������ɫ�Ǳ�����
	_pixel_operation1(m);
	//cv::imshow("pix_process", m);
	cv::imwrite(des_path, m);








//------------------------------------------------�ǵ���-------------------------------------------------------
	float sigma = 3.0;
	float threshold = 2.5;
	//! Parameters
	float fSigma = sigma;        //Ĭ��ֵ 2.0
	float anglePrecision = 7.5f;
	int connectivity = 2;          //! Connectivity for grouping       default: 2 (5x5x5)

	int nDir;
	nDir = (int)rintf(360 / anglePrecision);   //�������뷵��һ������


	//! First Inhibition
	float fThresholdLI = threshold;        //! Threshold for first inhibitions default: 2.5
	float fAngleFiltPrec = 15.0f;               //! degres used for inter-orientation inhibitions and filtering

	//! Directional Convolution
	float fCorThreshold = 0.275;
	fCorThreshold = fCorThreshold * (float)nDir;
	float minRespCorner = 0.005;
	float radForZone = fSigma;
	float fCorValidationDegree = 15.0;


	//! Corner clearing
	float radForClearCor = 2.0;          //! 3 * sigma, radius of circular zone to search for arriving contours


	//! Second Inhibition
	float fThresholdLISecondInhibitions = 0.005;

	//��һ�� uchar ת�� float
	cv::Mat m_grey;   //libUSTG::flimage inputImage = inputImageC.getGray();
	cv::cvtColor(m, m_grey, cv::COLOR_BGR2GRAY);

	int width = m_grey.cols;
	int height = m_grey.rows;
	int wh = width*height;

	//��̬������������������
	float *input = new float[width*height];
	uchar_float( m_grey ,  width, input);



	//!
	//! Compute directional derivatives
	//!
	float **dimages = new float*[nDir];                                               //ÿһ���Ƕ���һ���µ�ָ��
	for (int i = 0; i < nDir; i++)  dimages[i] = new float[width*height];             //ÿһ��ָ����һ�� width*height ��С�� ����dimages����ӦͼƬ�ݶȺ��cube
	printf("Compute directional convolutions ...\n");

	compute_directional_convolutions(input, dimages, nDir, fSigma, anglePrecision, width, height);   //



	//!
	//! Lateral inhibition
	//!
	printf("Lateral inhibition ...\n");

	float **dFilt = new float*[nDir];
	for (int i = 0; i < nDir; i++)  dFilt[i] = new float[wh];

	int flagKeepValue = 0;
	lateral_inhibition_each_scale(dimages, dFilt, fSigma, nDir, width, height, fAngleFiltPrec, fThresholdLI, anglePrecision, flagKeepValue);

	for (int i = 0; i < nDir; i++)  libUSTG::fpCopy(dFilt[i], dimages[i], width*height);  //ÿһ���Ƕȶ���һ����  ��ÿһ���㶼��һ��ԭͼ�Ĵ�С��ndir���Ƕȹ�����һ��cube

	//! Delete memory
	for (int i = 0; i < nDir; i++) delete[] dFilt[i];  //ɾ����dfilt  ÿ�εĽ����������dimages����
	delete[]  dFilt;




	//!
	//! Good continuation filtering
	//!
	printf("Good continuation filter ...\n");

	float **dFilt1 = new float*[nDir];
	for (int i = 0; i < nDir; i++)  dFilt1[i] = new float[wh];


	good_continuation_filter(dimages, dFilt1, fSigma, fAngleFiltPrec, nDir, anglePrecision, width, height);

	for (int i = 0; i < nDir; i++)  libUSTG::fpCopy(dFilt1[i], dimages[i], width*height);

	//!
	//! Delete memory
	//!
	for (int i = 0; i < nDir; i++) delete[] dFilt1[i];
	delete[]  dFilt1;




	//!
	//! Compute local maxima of transversal average as corner indicator
	//!
	float *localMax = new float[width*height];
	libUSTG::fpClear(localMax, 255.0, width*height);
	vector<str_point_descriptor> cCorners;
	printf("Compute corners ...\n");

	compute_corners(dimages, localMax, cCorners, nDir, fSigma, minRespCorner, fCorThreshold, radForZone, width, height);
	//��localMax ���滭�����еĹյ�
	//coner��candidate  ������cCorners


	printf("save...\n");
	

	save_lym(cCorners, fileCorners, m_grey, save_path);

	for (int i = 0; i < nDir; i++) delete[] dimages[i];
	delete[] dimages;


	///////////���Դ��� ����ɹ��� �ǲ��Ǿ�Ӧ������ͼƬ
	cv::imshow("det_point", m_grey);
	system("pause");
	cv::waitKey(0);

	}