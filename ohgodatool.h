#ifndef __OHGODATOOL_H
#define __OHGODATOOL_H

#include <stdint.h>
#include <stdbool.h>

#define OHGODATOOL_VERSION		"v1.2.1"

typedef struct _ArgsObj
{
	bool MemStateIdxProvided, CoreStateIdxProvided, SetMemClock, SetCoreClock, SetMemVDDCIdx, SetVTblVDD, SetFanspeed;
	bool GPUIdxProvided, VoltageStateIdxProvided, SetCoreVDDCIdx, SetVDDCI, SetMVDD, SetCoreVDDCOff, SetVDDCGFXOff;
	bool SetTDP, SetTDC, SetMaxPower, SetMaxCoreClk, SetMaxMemClk, VBIOSFileProvided;
	
	bool ShowMemStates, ShowCoreStates, ShowVoltageStates, ShowFanspeed, ShowTemp;
	
	uint32_t GPUIdx, MemStateIdx, CoreStateIdx, VoltStateIdx, ReqCoreClk, ReqMemClk, ReqVDDCI;
	uint32_t ReqMVDD, ReqCoreVoltTblIdx, ReqMemVoltTblIdx, ReqVoltTblVDDC, ReqFanspeedPercentage;
	uint32_t ReqTDP, ReqTDC, ReqMaxPower, ReqMaxCoreClk, ReqMaxMemClk;
	
	char *VBIOSFileName;
	
	int32_t ReqCoreVDDCOff, ReqVDDCGFXOff;
} ArgsObj;

bool ParseCmdLine(ArgsObj *Args, int argc, char **argv);

#endif

