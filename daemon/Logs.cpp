
#include "Logs.h"

istat::Log LogError(istat::LL_Error, "error");
istat::Log LogWarning(istat::LL_Warning, "warning");
istat::Log LogNotice(istat::LL_Notice, "notice");
istat::Log LogDebug(istat::LL_Debug, "debug");
istat::Log LogSpam(istat::LL_Spam, "spam");
