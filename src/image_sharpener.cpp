#include <iostream>
#include "libppm.h"
#include <cstdint>
#include<chrono>
using namespace std;

struct image_t* allocate_image(int width,int height){

	// Main Structure
	struct image_t* img  = new struct image_t;
	img->width = width;
	img->height = height;
	
	// Allocate Rows (Height)
	img->image_pixels = new uint8_t**[height];

	for(int i = 0; i < height; i++){
		// Allocate column for each row (Width)
		img->image_pixels[i] = new uint8_t*[width];
		for(int j = 0; j < width ; j++){
			// Allocate RGB channels for each pixel
			img->image_pixels[i][j] = new uint8_t[3];
		}
	}

	return img;

}

uint8_t clamp_to_byte(int val){
	if(val < 0){
		return 0;	
	}
	if(val > 255){
		return 255;
	}

	return (uint8_t)val;
}



struct image_t* S1_smoothen(struct image_t *input_image)
{
	int width  = input_image->width;
	int height = input_image->height;

	struct image_t* smoothened_image = allocate_image(width, height);

	for(int i = 0; i < height; i++)
	{
		for(int j = 0; j < width; j++)
		{
			for(int k = 0; k < 3; k++)
			{
				int sum   = 0;
				int count = 0;

				for(int di = -1; di <= 1; di++)
				{
					for(int dj = -1; dj <= 1; dj++)
					{
						int ni = i + di;
						int nj = j + dj;

						// if(ni < 0) ni = 0;
						// if(ni >= height) ni = height - 1;
						// if(nj < 0) nj = 0;
						// if(nj >= width) nj = width - 1;

						// sum += input_image->image_pixels[ni][nj][k];
						// count++;

                        // Only add to the sum if the neighbor actually exists inside the image
                        if(ni >= 0 && ni < height && nj >= 0 && nj < width){
                            sum += input_image->image_pixels[ni][nj][k];
                            ++count;
                        }
					}
				}

				smoothened_image->image_pixels[i][j][k] = (uint8_t)(sum / count);
			}
		}
	}

	return smoothened_image;
}

struct image_t* S2_find_details(struct image_t *input_image, struct image_t *smoothened_image)
{
	int width  = input_image->width;
	int height = input_image->height;

	struct image_t* details_image = allocate_image(width, height);

	for(int i = 0; i < height; i++)
	{
		for(int j = 0; j < width; j++)
		{
			for(int k = 0; k < 3; k++)
			{
				int diff = input_image->image_pixels[i][j][k] - smoothened_image->image_pixels[i][j][k]; 
				// details_image->image_pixels[i][j][k] = clamp_to_byte(diff);
                // Assign directly without clamping to preserve negative values
                details_image->image_pixels[i][j][k] = diff;
			}
		}
	}

	return details_image;
}

struct image_t* S3_sharpen(struct image_t *input_image, struct image_t *details_image)
{
	int width  = input_image->width;
	int height = input_image->height;

	struct image_t* sharpened_image = allocate_image(width, height);

	for(int i = 0; i < height; i++)
	{
		for(int j = 0; j < width; j++)
		{
			for(int k = 0; k < 3; k++)
			{
				int original = (int)input_image->image_pixels[i][j][k];
				int detail = (int8_t)details_image->image_pixels	[i][j][k];
				sharpened_image->image_pixels[i][j][k] = clamp_to_byte(original + detail);
			}
		}
	}

	return sharpened_image;
}

int main(int argc, char **argv)
{
	if(argc != 3)
	{
		cout << "usage: ./a.out <path-to-original-image> <path-to-transformed-image>\n\n";
		exit(0);
	}

    //Clock Start
	auto t1 = chrono::high_resolution_clock::now();

	struct image_t *input_image = read_ppm_file(argv[1]);

	auto f1 = chrono::high_resolution_clock::now();

	struct image_t *smoothened_image = S1_smoothen(input_image);

	auto t2 = chrono::high_resolution_clock::now();

	struct image_t *details_image = S2_find_details(input_image, smoothened_image);
	
    auto f2 = chrono::high_resolution_clock::now();

	struct image_t *sharpened_image = S3_sharpen(input_image, details_image);
	
    auto t3 = chrono::high_resolution_clock::now();

	write_ppm_file(argv[2], sharpened_image);
	
    auto f3 = chrono::high_resolution_clock::now();        

    //Calculating Duration

    double read_time = chrono::duration<double,milli>(f1-t1).count();
    double s1_time = chrono::duration<double,milli>(t2-f1).count();
    double s2_time = chrono::duration<double,milli>(f2-t2).count();
    double s3_time = chrono::duration<double,milli>(t3-f2).count();
    double write_time = chrono::duration<double,milli>(f3-t3).count();
    

    cout << "METRICS: " << read_time << " " << s1_time << " " << s2_time << " " << s3_time << " " << write_time << endl;
	return 0;
}
