#include <iostream>
#include "libppm.h"
#include <cstdint>

using namespace std;

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

						if(ni < 0) ni = 0;
						if(ni >= height) ni = height - 1;
						if(nj < 0) nj = 0;
						if(nj >= width) nj = width - 1;

						sum += input_image->image_pixels[ni][nj][k];
						count++;
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
				int diff = (int)input_image->image_pixels[i][j][k]
				         - (int)smoothened_image->image_pixels[i][j][k];
				details_image->image_pixels[i][j][k] = clamp_to_byte(diff);
			}
		}
	}

	return details_image;
}

struct image_t* S3_sharpen(struct image_t *input_image, struct image_t *details_image)
{
	// TODO
	return input_image; //TODO remove this line when adding your code
}

int main(int argc, char **argv)
{
	if(argc != 3)
	{
		cout << "usage: ./a.out <path-to-original-image> <path-to-transformed-image>\n\n";
		exit(0);
	}
	
	struct image_t *input_image = read_ppm_file(argv[1]);
	
	struct image_t *smoothened_image = S1_smoothen(input_image);
	
	struct image_t *details_image = S2_find_details(input_image, smoothened_image);
	
	struct image_t *sharpened_image = S3_sharpen(input_image, details_image);
	
	write_ppm_file(argv[2], sharpened_image);
	
	return 0;
}
