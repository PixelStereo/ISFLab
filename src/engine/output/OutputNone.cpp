// The outputs that are not built into this executable (platform or CMake option): no object, outputCompiled() is false.
#include "VideoOutput.h"

#ifndef ISFLAB_OUTPUT_SYPHON
std::unique_ptr<GpuOutput> createSyphonOutput() { return nullptr; }
#endif
#ifndef ISFLAB_OUTPUT_SPOUT
std::unique_ptr<GpuOutput> createSpoutOutput() { return nullptr; }
#endif
#ifndef ISFLAB_OUTPUT_V4L2
std::unique_ptr<CpuOutput> createV4l2Output() { return nullptr; }
#endif
#ifndef ISFLAB_OUTPUT_OMT
std::unique_ptr<CpuOutput> createOmtOutput() { return nullptr; }
#endif
