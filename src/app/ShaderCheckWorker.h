#pragma once
#include <QString>

// The batch shader check runs in a separate ISFLab process, because some shaders crash the OpenGL
// driver while it compiles them. The worker reads one shader path per line from listFile and writes
// to stdout, for each one, "BEGIN <index>" before compiling it and then "OK <index>" or
// "ERR <index> <percent-encoded message>". "FAIL <message>" means it could not start checking.
inline constexpr auto ShaderCheckWorkerFlag = "--check-shaders";
int runShaderCheckWorker(int argc, char **argv);
