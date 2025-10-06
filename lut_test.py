from PIL import Image
import numpy as np
import math

# https://kevinmartinjose.com/2021/04/27/film-simulations-from-scratch-using-python/
def apply_hald_clut(hald_img, img):
    hald_w, hald_h = hald_img.size
    clut_size = int(round(math.pow(hald_w, 1/3)))
    # We square the clut_size because a 12-bit HaldCLUT has the same amount of information as a 144-bit 3D CLUT
    scale = (clut_size * clut_size - 1) / 255
    # Convert the PIL image to numpy array
    img = np.asarray(img)
    # We are reshaping to (144 * 144 * 144, 3) - it helps with indexing
    hald_img = np.asarray(hald_img).reshape(clut_size ** 6, 3)
    # Figure out the 3D CLUT indexes corresponding to the pixels in our image
    clut_r = np.rint(img[:, :, 0] * scale).astype(int)
    clut_g = np.rint(img[:, :, 1] * scale).astype(int)
    clut_b = np.rint(img[:, :, 2] * scale).astype(int)
    filtered_image = np.zeros((img.shape))
    # Convert the 3D CLUT indexes into indexes for our HaldCLUT numpy array and copy over the colors to the new image
    filtered_image[:, :] = hald_img[clut_r + clut_size ** 2 * clut_g + clut_size ** 4 * clut_b]
    filtered_image = Image.fromarray(filtered_image.astype('uint8'), 'RGB')
    return filtered_image

lut = Image.open("Fuji Velvia 50.png")
source = Image.open("test.jpg")

filtered = apply_hald_clut(lut, source)
filtered.save('out.jpg')
