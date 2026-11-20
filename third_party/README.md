These libraries receive video shared by other applications, and share ISFLab's rendered picture with them (the Output menu). ISFLab uses the following pinned upstream submodules:

- [Syphon Framework](https://github.com/Syphon/Syphon-Framework), revision `f4761677a45b8034a3c2069ec0f3d2553da81fba`. See `syphon/License.txt`.
- [Spout2](https://github.com/leadedge/Spout2), revision `c2bcc12147711d12ace7d5f08e869d774d840f8a`. See `spout/LICENSE`.

OMT (Open Media Transport, [libomt and libvmx](https://github.com/openmediatransport), MIT license) is not built with ISFLab: the OMT output loads the libraries at run time, when they are installed next to the application or in the system's library folder. The virtual webcam output uses the Linux kernel's V4L2 interface (`linux/videodev2.h`) and the v4l2loopback module, both GPL-licensed and neither linked nor shipped.
