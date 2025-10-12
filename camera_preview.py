from picamera2 import Picamera2, Preview
import time

picam2 = Picamera2()

picam2.start_preview(Preview.DRM,x=0,y=0,width=480,height=640)


preview_config = picam2.create_preview_configuration(lores={'size': (640,480)}) 
picam2.configure(preview_config)
picam2.start()


time.sleep(5)

picam2.stop_preview()
