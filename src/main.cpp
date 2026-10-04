#include "amxxmodule.h"

void OnAmxxAttach()
{
	MF_Log("%s v%s loaded.", MODULE_NAME, MODULE_VERSION);
}

void OnAmxxDetach()
{
	MF_Log("%s v%s unloaded.", MODULE_NAME, MODULE_VERSION);
}
