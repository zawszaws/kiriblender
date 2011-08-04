import bpy
camera = bpy.context.edit_movieclip.tracking.camera

camera.sensor_width = 22.3
camera.sensor_height = 14.9
camera.units = 'MILLIMETERS'
camera.focal_length = 24.0
camera.k1 = 0.0
camera.k2 = 0.0
camera.k3 = 0.0
