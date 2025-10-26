Documentation
-

you can use/copy this code idgaf. Something I just made for fun and to get better at coding

**What does it do?**

- cheap device (~30ish usd) that you can attach to your car that detects potholes
- potholes are automatically put onto (map)[https://ezpothole.vercel.app/] and stored in database
- you can upvote/downvote to give local government an idea of what potholes are the highest priority/most annoying
- iOS app for location tracking that connects to the device via bluetooth

**Where's the web app/ios app code**
(web)[https://github.com/aidenjcheng/pothole-website]
(ios)[https://github.com/aidenjcheng/ezpothole-ios]

**Where's the firmware/3d model**

in this repo in / firmware

**TODO/PLANNED FEATURES:**
- admin mode (resolve potholes)
- better documentation for assembly of camera

**Whats the point of the iOS app?**
GPS module for XIAO ESP32 S3 SENSE doesn't ship to US unfortunately so I just used the iOS app to track data since it was already being used for hotspot for the camera. 
