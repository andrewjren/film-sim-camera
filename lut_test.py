from PIL import Image
import numpy as np

image = Image.open("Fuji Velvia 50.png")
image_array = np.array(image)
print(image_array.shape)

