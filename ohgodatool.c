// Copyright (c) 2017 OhGodACompany - OhGodAGirl & OhGodAPet

#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include "ohgodatool.h"
#include "ohgodatool-common.h"
#include "vbios-tables.h"

uint32_t GetPPTableOffset(uint8_t *VBIOS)
{
	ATOM_ROM_HEADER *hdr = (ATOM_ROM_HEADER *)(VBIOS + ((uint16_t *)(VBIOS + OFFSET_TO_POINTER_TO_ATOM_ROM_HEADER))[0]);
	ATOM_MASTER_LIST_OF_DATA_TABLES *DataTblList = &((ATOM_MASTER_DATA_TABLE *)(VBIOS + hdr->usMasterDataTableOffset))->ListOfDataTables;
	return(DataTblList->PowerPlayInfo);
}

int main(int argc, char **argv)
{
	uint32_t VBIOSFileOffset;
	struct stat FileStats;
	char FilePath[64];
	uint8_t *PPTblBuf;
	size_t BytesRead;
	ArgsObj Config;
	int PPTblSize;
	FILE *PPFile;
	
	if(!ParseCmdLine(&Config, argc, argv)) return(-1);
	
	if(!Config.GPUIdxProvided && !Config.VBIOSFileProvided)
	{
		printf("GPU index is required when editing a live PowerPlay table.\n");
		return(-1);
	}
	
	if(Config.VBIOSFileProvided)
	{
		uint8_t *VBIOS;
		
		PPFile = fopen(Config.VBIOSFileName, "rb+");
		
		if(!PPFile)
		{
			printf("Error opening %s (are you root, and does it exist?)\n", Config.VBIOSFileName);
			free(Config.VBIOSFileName);
			fclose(PPFile);
			return(-1);
		}
		
		stat(Config.VBIOSFileName, &FileStats);
		
		if(FileStats.st_size > 1048576)
		{
			printf("File too large to be a VBIOS.\n");
			free(Config.VBIOSFileName);
			fclose(PPFile);
			return(-1);
		}
		
		VBIOS = (uint8_t *)malloc(sizeof(uint8_t) * FileStats.st_size);
		
		if(fread(VBIOS, sizeof(uint8_t), FileStats.st_size, PPFile) != FileStats.st_size)
		{
			printf("Unable to read VBIOS file.\n");
			free(Config.VBIOSFileName);
			fclose(PPFile);
			return(-1);
		}
		
		free(Config.VBIOSFileName);
		VBIOSFileOffset = GetPPTableOffset(VBIOS);
		fseek(PPFile, VBIOSFileOffset, SEEK_SET);
		free(VBIOS);
	}
	else
	{
		sprintf(FilePath, "/sys/class/drm/card%d/device/pp_table", Config.GPUIdx);
		
		PPFile = fopen(FilePath, "rb+");
		
		if(!PPFile)
		{
			printf("Error opening %s (are you root, and does it exist?)\n", FilePath);
			return(-1);
		}
	}
	
	PPTblBuf = (uint8_t *)malloc(sizeof(AtomBIOSCommonTableHeader));
	
	BytesRead = fread(PPTblBuf, 1, sizeof(AtomBIOSCommonTableHeader), PPFile);
	
	if(BytesRead != sizeof(AtomBIOSCommonTableHeader))
	{
		printf("Unable to read header from file.\n");
		free(PPTblBuf);
		fclose(PPFile);
		return(-1);
	}
	
	PPTblSize = ((AtomBIOSCommonTableHeader *)PPTblBuf)->Size;
	
	PPTblBuf = realloc(PPTblBuf, sizeof(AtomBIOSCommonTableHeader) + PPTblSize);
	
	if(errno == ENOMEM)
	{
		printf("Unable to allocate enough memory for the PowerPlay table.\n");
		free(PPTblBuf);
		fclose(PPFile);
		return(-1);
	}
	
	BytesRead += fread(PPTblBuf + sizeof(AtomBIOSCommonTableHeader), 1, PPTblSize, PPFile);
	
	if(Config.VBIOSFileProvided) BytesRead -= sizeof(AtomBIOSCommonTableHeader);
	
	if(BytesRead != PPTblSize)
	{
		printf("Unable to read entire PowerPlay table. (read %d, size was %d)\n", BytesRead, PPTblSize);
		free(PPTblBuf);
		fclose(PPFile);
		return(-1);
	}
	
	PPTableHeader *PPHdr = (PPTableHeader *)PPTblBuf;
	
	if(Config.SetMaxCoreClk) PPHdr->MaxODCoreClk = Config.ReqMaxCoreClk;
	if(Config.SetMaxMemClk) PPHdr->MaxODMemClk = Config.ReqMaxMemClk;
	
	uint32_t TotalStates;
	
	TotalStates = ((PolarisMemClkDepTable *)(PPTblBuf + PPHdr->MemClkDepTableOffset))->NumEntries;
	
	if(Config.MemStateIdxProvided && TotalStates <= Config.MemStateIdx)
	{
		printf("Specified memory state does not exist.\n");
		free(PPTblBuf);
		fclose(PPFile);
		return(-1);
	}
	
	// TODO: Add checking that offsets don't exceed table size.
	PolarisMemClkDepRecord *MemClkRecords = ((PolarisMemClkDepTable *)(PPTblBuf + PPHdr->MemClkDepTableOffset))->Entries;
	PolarisVoltageLookupTable *VoltageTbl = (PolarisVoltageLookupTable *)(PPTblBuf + PPHdr->VDDCLookupTableOffset);
	
	if(Config.MemStateIdxProvided)
	{
		if(Config.SetMemClock)
		{
			printf("Memory state %d clock: %d -> %d.\n", Config.MemStateIdx, MemClkRecords[Config.MemStateIdx].MemClk / 100, Config.ReqMemClk);
			MemClkRecords[Config.MemStateIdx].MemClk = Config.ReqMemClk * 100;
		}
		
		if(Config.SetMemVDDCIdx)
		{
			if(Config.ReqMemVoltTblIdx >= VoltageTbl->NumEntries)
			{
				printf("Voltage table index provided does not exist. Refusing to set.\n");
			}
			else
			{
				printf("Memory state %d VDDC: %d -> %d.\n", Config.MemStateIdx, MemClkRecords[Config.MemStateIdx].VDDC, Config.ReqMemVoltTblIdx);
				MemClkRecords[Config.MemStateIdx].VDDC = Config.ReqMemVoltTblIdx;
			}
		}
		
		if(Config.SetVDDCI)
		{
			printf("Memory state %d VDDCI: %d -> %d.\n", Config.MemStateIdx, MemClkRecords[Config.MemStateIdx].VDDCI, Config.ReqVDDCI);
			MemClkRecords[Config.MemStateIdx].VDDCI = Config.ReqVDDCI;
		}
		
		if(Config.SetMVDD)
		{
			printf("Memory state %d MVDD: %d -> %d.\n", Config.MemStateIdx, MemClkRecords[Config.MemStateIdx].MVDD, Config.ReqMVDD);
			MemClkRecords[Config.MemStateIdx].MVDD = Config.ReqMVDD;
		}
		
		if(Config.SetVDDCGFXOff)
		{
			printf("Memory state %d VDDC GFX offset: %d -> %d.\n", Config.MemStateIdx, MemClkRecords[Config.MemStateIdx].VDDCGFXOffset, Config.ReqVDDCGFXOff);
			MemClkRecords[Config.MemStateIdx].VDDCGFXOffset = Config.ReqVDDCGFXOff;
		}
	}
	
	if(Config.ShowMemStates)
	{
		if(!Config.MemStateIdxProvided)
		{
			for(int i = 0; i < TotalStates; ++i)
			{
				if(MemClkRecords[i].VDDC >= VoltageTbl->NumEntries)
				{
					printf("Error in table - DPM state references non-existant voltage entry.\n");
					free(PPTblBuf);
					fclose(PPFile);
					return(-1);
				}
				
				printf("Memory state %d:\n", i);
				printf("\tVDDC: %d\n", VoltageTbl->Entries[i].VDD);
				printf("\tVDDCI: %d\n", MemClkRecords[i].VDDCI);
				printf("\tVDDC GFX offset: %d\n", MemClkRecords[i].VDDCGFXOffset);
				printf("\tMVDD: %d\n", MemClkRecords[i].MVDD);
				printf("\tMemory clock: %d\n", MemClkRecords[i].MemClk / 100);
			}
		}
		else
		{
			if(MemClkRecords[Config.MemStateIdx].VDDC >= VoltageTbl->NumEntries)
			{
				printf("Error in table - DPM state references non-existant voltage entry.\n");
				free(PPTblBuf);
				fclose(PPFile);
				return(-1);
			}
			
			printf("Memory state %d:\n", Config.MemStateIdx);
			printf("\tVDDC: %d\n", VoltageTbl->Entries[Config.MemStateIdx].VDD);
			printf("\tVDDCI: %d\n", MemClkRecords[Config.MemStateIdx].VDDCI);
			printf("\tVDDC offset: %d\n", MemClkRecords[Config.MemStateIdx].VDDCGFXOffset);
			printf("\tMVDD: %d\n", MemClkRecords[Config.MemStateIdx].MVDD);
			printf("\tMemory clock: %d\n", MemClkRecords[Config.MemStateIdx].MemClk / 100);
		}
		
		putchar('\n');
	}
	
	TotalStates = ((PolarisCoreClkDepTable *)(PPTblBuf + PPHdr->CoreClkDepTableOffset))->NumEntries;
	if(((PolarisCoreClkDepTable *)(PPTblBuf + PPHdr->CoreClkDepTableOffset))->RevisionID < 1)
	{
		TongaCoreClkDepRecord *CoreClkRecords = ((TongaCoreClkDepTable *)(PPTblBuf + PPHdr->CoreClkDepTableOffset))->Entries;
		
		if(Config.CoreStateIdxProvided && TotalStates <= Config.CoreStateIdx)
		{
			printf("Specified core state does not exist.\n");
			free(PPTblBuf);
			fclose(PPFile);
			return(-1);
		}
		
		if(Config.CoreStateIdxProvided)
		{
			if(Config.SetCoreClock)
			{
				printf("DPM state %d core clock: %d -> %d.\n", Config.CoreStateIdx, CoreClkRecords[Config.CoreStateIdx].CoreClk / 100, Config.ReqCoreClk);
				CoreClkRecords[Config.CoreStateIdx].CoreClk = Config.ReqCoreClk * 100;
			}
			
			if(Config.SetCoreVDDCIdx)
			{
				if(Config.ReqCoreVoltTblIdx >= VoltageTbl->NumEntries)
				{
					printf("Voltage table index provided does not exist. Refusing to set.\n");
				}
				else
				{
					printf("Core state %d VDDC: %d -> %d.\n", Config.CoreStateIdx, CoreClkRecords[Config.CoreStateIdx].VDDC, Config.ReqCoreVoltTblIdx);
					CoreClkRecords[Config.CoreStateIdx].VDDC = Config.ReqCoreVoltTblIdx;
				}
			}
			
			if(Config.SetCoreVDDCOff)
			{
				printf("DPM state %d VDDC offset: %d -> %d.\n", Config.CoreStateIdx, CoreClkRecords[Config.CoreStateIdx].VDDCOffset, Config.ReqCoreVDDCOff);
				CoreClkRecords[Config.CoreStateIdx].VDDCOffset = Config.ReqCoreVDDCOff;
			}
		}
		
		if(Config.ShowCoreStates)
		{
			if(!Config.CoreStateIdxProvided)
			{
				for(int i = 0; i < TotalStates; ++i)
				{
					if(CoreClkRecords[i].VDDC > VoltageTbl->NumEntries)
					{
						printf("Error in table - DPM state references non-existant voltage entry.\n");
						free(PPTblBuf);
						fclose(PPFile);
						return(-1);
					}
					
					printf("DPM state %d:\n", i);
					printf("\tVDDC: %d (voltage table entry %d)\n", VoltageTbl->Entries[CoreClkRecords[i].VDDC].VDD, CoreClkRecords[i].VDDC);
					printf("\tVDDC offset: %d\n", CoreClkRecords[i].VDDCOffset);
					printf("\tCore clock: %d\n", CoreClkRecords[i].CoreClk / 100);
				}
			}
			else
			{
				if(CoreClkRecords[Config.CoreStateIdx].VDDC >= VoltageTbl->NumEntries)
				{
					printf("Error in table - DPM state references non-existant voltage entry.\n");
					free(PPTblBuf);
					fclose(PPFile);
					return(-1);
				}
				
				printf("DPM state %d:\n", Config.CoreStateIdx);
				printf("\tVDDC: %d (voltage table entry %d)\n", VoltageTbl->Entries[CoreClkRecords[Config.CoreStateIdx].VDDC].VDD, CoreClkRecords[Config.CoreStateIdx].VDDC);
				printf("\tVDDC offset: %d\n", CoreClkRecords[Config.CoreStateIdx].VDDCOffset);
				printf("\tCore clock: %d\n", CoreClkRecords[Config.CoreStateIdx].CoreClk / 100);
			}
			
			putchar('\n');
		}
	}
	else
	{
		PolarisCoreClkDepRecord *CoreClkRecords = ((PolarisCoreClkDepTable *)(PPTblBuf + PPHdr->CoreClkDepTableOffset))->Entries;
	
		if(Config.CoreStateIdxProvided && TotalStates <= Config.CoreStateIdx)
		{
			printf("Specified core state does not exist.\n");
			free(PPTblBuf);
			fclose(PPFile);
			return(-1);
		}
		
		if(Config.CoreStateIdxProvided)
		{
			if(Config.SetCoreClock)
			{
				printf("DPM state %d core clock: %d -> %d.\n", Config.CoreStateIdx, CoreClkRecords[Config.CoreStateIdx].CoreClk / 100, Config.ReqCoreClk);
				CoreClkRecords[Config.CoreStateIdx].CoreClk = Config.ReqCoreClk * 100;
			}
			
			if(Config.SetCoreVDDCIdx)
			{
				if(Config.ReqCoreVoltTblIdx >= VoltageTbl->NumEntries)
				{
					printf("Voltage table index provided does not exist. Refusing to set.\n");
				}
				else
				{
					printf("Core state %d VDDC: %d -> %d.\n", Config.CoreStateIdx, CoreClkRecords[Config.CoreStateIdx].VDDC, Config.ReqCoreVoltTblIdx);
					CoreClkRecords[Config.CoreStateIdx].VDDC = Config.ReqCoreVoltTblIdx;
				}
			}
			
			if(Config.SetCoreVDDCOff)
			{
				printf("DPM state %d VDDC offset: %d -> %d.\n", Config.CoreStateIdx, CoreClkRecords[Config.CoreStateIdx].VDDCOffset, Config.ReqCoreVDDCOff);
				CoreClkRecords[Config.CoreStateIdx].VDDCOffset = Config.ReqCoreVDDCOff;
			}
		}
		
		if(Config.ShowCoreStates)
		{
			if(!Config.CoreStateIdxProvided)
			{
				for(int i = 0; i < TotalStates; ++i)
				{
					if(CoreClkRecords[i].VDDC >= VoltageTbl->NumEntries)
					{
						printf("Error in table - DPM state references non-existant voltage entry.\n");
						free(PPTblBuf);
						fclose(PPFile);
						return(-1);
					}
					
					printf("DPM state %d:\n", i);
					printf("\tVDDC: %d (voltage table entry %d)\n", VoltageTbl->Entries[CoreClkRecords[i].VDDC].VDD, CoreClkRecords[i].VDDC);
					printf("\tVDDC offset: %d\n", CoreClkRecords[i].VDDCOffset);
					printf("\tCore clock: %d\n", CoreClkRecords[i].CoreClk / 100);
				}
			}
			else
			{
				if(CoreClkRecords[Config.CoreStateIdx].VDDC >= VoltageTbl->NumEntries)
				{
					printf("Error in table - DPM state references non-existant voltage entry.\n");
					free(PPTblBuf);
					fclose(PPFile);
					return(-1);
				}
				
				printf("DPM state %d:\n", Config.CoreStateIdx);
				printf("\tVDDC: %d (voltage table entry %d)\n", VoltageTbl->Entries[CoreClkRecords[Config.CoreStateIdx].VDDC].VDD, CoreClkRecords[Config.CoreStateIdx].VDDC);
				printf("\tVDDC offset: %d\n", CoreClkRecords[Config.CoreStateIdx].VDDCOffset);
				printf("\tCore clock: %d\n", CoreClkRecords[Config.CoreStateIdx].CoreClk / 100);
			}
			
			putchar('\n');
		}
	}
	
	TotalStates = VoltageTbl->NumEntries;
	PolarisVoltageLookupRecord *VoltageStateRecords = VoltageTbl->Entries;
	PolarisVoltageLookupRecord *GFXVoltageStateRecords = ((PolarisVoltageLookupTable *)(PPTblBuf + PPHdr->VDDCGFXLookupTableOffset))->Entries;
	uint32_t GFXTotalStates = ((PolarisVoltageLookupTable *)(PPTblBuf + PPHdr->VDDCGFXLookupTableOffset))->NumEntries;
	
	if(Config.VoltageStateIdxProvided && TotalStates <= Config.VoltStateIdx)
	{
		printf("Specified voltage state does not exist.\n");
		free(PPTblBuf);
		fclose(PPFile);
		return(-1);
	}
	
	if(Config.VoltageStateIdxProvided)
	{
		if(Config.SetVTblVDD)
		{
			printf("Voltage table index %d: %d -> %d.\n", Config.VoltStateIdx, VoltageStateRecords[Config.VoltStateIdx].VDD, Config.ReqVoltTblVDDC);
			VoltageStateRecords[Config.VoltStateIdx].VDD = Config.ReqVoltTblVDDC;
			if(Config.VoltStateIdx < GFXTotalStates) GFXVoltageStateRecords[Config.VoltStateIdx].VDD = Config.ReqVoltTblVDDC;
		}
	}
	
	if(Config.ShowVoltageStates)
	{
		if(!Config.VoltageStateIdxProvided)
		{
			for(int i = 0; i < TotalStates; ++i)
			{
				printf("Voltage state %d: \n\tVDD = %d\n\tCACLow = %d\n\tCACMid = %d\n\tCACHigh = %d\n", i, VoltageStateRecords[i].VDD, VoltageStateRecords[i].CACLow, VoltageStateRecords[i].CACMid, VoltageStateRecords[i].CACHigh);
			}			
		}
		else
		{
			printf("Voltage state %d: \n\tVDD = %d\n\tCACLow = %d\n\tCACMid = %d\n\tCACHigh = %d\n", Config.VoltageStateIdxProvided, VoltageStateRecords[Config.VoltStateIdx].VDD, VoltageStateRecords[Config.VoltStateIdx].CACLow, VoltageStateRecords[Config.VoltStateIdx].CACMid, VoltageStateRecords[Config.VoltStateIdx].CACHigh);
		}
		
		putchar('\n');
	}
	
	if(((PolarisPowerTuneTable *)(PPTblBuf + PPHdr->PowerTuneTableOffset))->RevId < 3)
	{
		
		TongaPowerTuneTable *PwrTuneTbl = (TongaPowerTuneTable *)(PPTblBuf + PPHdr->PowerTuneTableOffset);
	
		if(Config.SetMaxPower)
		{
			printf("Max Power Delivery Limit: %dW -> %dW.\n", PwrTuneTbl->MaximumPowerDeliveryLimit, Config.ReqMaxPower);
			PwrTuneTbl->MaximumPowerDeliveryLimit = Config.ReqMaxPower;
		}
		if(Config.SetTDP)
		{
			printf("TDP: %dW -> %dW.\n", PwrTuneTbl->TDP, Config.ReqTDP);
			PwrTuneTbl->TDP = Config.ReqTDP;
		}
		if(Config.SetTDC)
		{
			printf("TDC: %dW -> %dW.\n", PwrTuneTbl->TDC, Config.ReqTDC);
			PwrTuneTbl->TDC = Config.ReqTDC;
		}
	}
	else
	{
		PolarisPowerTuneTable *PwrTuneTbl = (PolarisPowerTuneTable *)(PPTblBuf + PPHdr->PowerTuneTableOffset);
		
		if(Config.SetMaxPower)
		{
			printf("Max Power Delivery Limit: %dW -> %dW.\n", PwrTuneTbl->MaximumPowerDeliveryLimit, Config.ReqMaxPower);
			PwrTuneTbl->MaximumPowerDeliveryLimit = Config.ReqMaxPower;
		}
		if(Config.SetTDP)
		{
			printf("TDP: %dW -> %dW.\n", PwrTuneTbl->TDP, Config.ReqTDP);
			PwrTuneTbl->TDP = Config.ReqTDP;
		}
		if(Config.SetTDC)
		{
			printf("TDC: %dW -> %dW.\n", PwrTuneTbl->TDC, Config.ReqTDC);
			PwrTuneTbl->TDC = Config.ReqTDC;
		}
	}
	if(Config.SetMemClock || Config.SetCoreClock || Config.SetMemVDDCIdx || Config.SetVTblVDD || Config.SetCoreVDDCIdx || Config.SetVDDCI || Config.SetMVDD || Config.SetCoreVDDCOff || Config.SetVDDCGFXOff || \
		Config.SetMaxPower || Config.SetTDP || Config.SetTDC || Config.SetMaxCoreClk || Config.SetMaxMemClk)
	{
		if(Config.VBIOSFileProvided)
			fseek(PPFile, VBIOSFileOffset, SEEK_SET);
		
		fwrite(PPTblBuf, sizeof(uint8_t), BytesRead, PPFile);
	}
	
	free(PPTblBuf);
	fclose(PPFile);
	
	if(Config.ShowFanspeed || Config.SetFanspeed || Config.ShowTemp)
	{
		char *GPUHWMonDir;
		
		GetGPUHWMonPath(&GPUHWMonDir, Config.GPUIdx);
		
		if(!GPUHWMonDir)
		{
			printf("Exiting due to failure to get HWMon directory for GPU.\n");
			return(1);
		}
		
		if(Config.ShowTemp)
		{
			char *GPUTemperatureSysFSFileName;	
			
			GPUTemperatureSysFSFileName = (char *)malloc(sizeof(char) * (256 + strlen(GPUHWMonDir)));
			sprintf(GPUTemperatureSysFSFileName, "%s/temp1_input", GPUHWMonDir);
			
			FILE *SysFSTemperatureFile = fopen(GPUTemperatureSysFSFileName, "rb");
		
			if(!SysFSTemperatureFile)
			{
				printf("Failed to open temperature sysfs entry for GPU %d.\n", Config.GPUIdx);
				free(GPUHWMonDir);
				free(GPUTemperatureSysFSFileName);
				return(1);
			}
		
			char TemperatureString[32];
			size_t BytesRead;
					
			BytesRead = fread(TemperatureString, sizeof(char), 32, SysFSTemperatureFile);
			
			if(BytesRead <= 0)
			{
				printf("Failed to read temperature sysfs entry for GPU %d.\n", Config.GPUIdx);
				free(GPUHWMonDir);
				free(GPUTemperatureSysFSFileName);
				fclose(SysFSTemperatureFile);
				return(1);
			}
			
			TemperatureString[BytesRead] = 0x00;
			
			uint32_t Temperature = strtoul(TemperatureString, NULL, 10);
		
			printf("%dC\n", Temperature / 1000);
			
			free(GPUTemperatureSysFSFileName);
			fclose(SysFSTemperatureFile);
		}
	
		if(Config.SetFanspeed || Config.ShowFanspeed)
		{
			char *GPUFanMaxSysFSFileName = (char *)malloc(sizeof(char) * (256 + strlen(GPUHWMonDir)));
			char *GPUFanSysFSFileName = (char *)malloc(sizeof(char) * (256 + strlen(GPUHWMonDir)));
			char FanSettingString[32];
			uint32_t FanMaxSetting;
			size_t BytesRead;
			
			sprintf(GPUFanMaxSysFSFileName, "%s/pwm1_max", GPUHWMonDir);
			
			FILE *GPUFanMaxFile = fopen(GPUFanMaxSysFSFileName, "rb");
			
			if(!GPUFanMaxFile)
			{
				printf("Failed to open a fan sysfs entry for GPU %d.\n", Config.GPUIdx);
				free(GPUHWMonDir);
				free(GPUFanMaxSysFSFileName);
				free(GPUFanSysFSFileName);
				return(1);
			}
			
			BytesRead = fread(FanSettingString, sizeof(char), 32, GPUFanMaxFile);
			
			if(BytesRead <= 0)
			{
				printf("Failed to read a fan sysfs entry for GPU %d.\n", Config.GPUIdx);
				free(GPUHWMonDir);
				free(GPUFanMaxSysFSFileName);
				free(GPUFanSysFSFileName);
				fclose(GPUFanMaxFile);
				return(1);
			}
			
			FanSettingString[BytesRead] = 0x00;
			FanMaxSetting = strtoul(FanSettingString, NULL, 10);
			fclose(GPUFanMaxFile);
			free(GPUFanMaxSysFSFileName);
			
			sprintf(GPUFanSysFSFileName, "%s/pwm1", GPUHWMonDir);
			
			if(Config.SetFanspeed)
			{
				FILE *GPUFanFile = fopen(GPUFanSysFSFileName, "rb+");
				uint32_t OldFanSetting;
				
				if(!GPUFanFile)
				{
					printf("Failed to open a fan sysfs entry for GPU %d.\n", Config.GPUIdx);
					free(GPUHWMonDir);
					free(GPUFanSysFSFileName);
					return(1);
				}
				
				BytesRead = fread(FanSettingString, sizeof(char), 32, GPUFanFile);
			
				if(BytesRead <= 0)
				{
					printf("Failed to read a fan sysfs entry for GPU %d.\n", Config.GPUIdx);
					free(GPUHWMonDir);
					free(GPUFanSysFSFileName);
					fclose(GPUFanFile);
					return(1);
				}
				
				FanSettingString[BytesRead] = 0x00;
				OldFanSetting = strtoul(FanSettingString, NULL, 10);
				
				OldFanSetting = (int)round(((double)OldFanSetting * 100.0) / (double)FanMaxSetting);
				
				rewind(GPUFanFile);
				
				sprintf(FanSettingString, "%d", (int)round((double)FanMaxSetting * (((double)Config.ReqFanspeedPercentage) / 100.0)));
				
				fwrite(FanSettingString, sizeof(char), strlen(FanSettingString), GPUFanFile);
				
				printf("GPU %d Fanspeed %d%% -> %d%%\n", Config.GPUIdx, OldFanSetting, Config.ReqFanspeedPercentage);
				fclose(GPUFanFile);
			}
			if(Config.ShowFanspeed)
			{
				FILE *GPUFanFile = fopen(GPUFanSysFSFileName, "rb+");
				uint32_t FanSetting;
				
				if(!GPUFanFile)
				{
					printf("Failed to open a fan sysfs entry for GPU %d.\n", Config.GPUIdx);
					free(GPUHWMonDir);
					free(GPUFanSysFSFileName);
					return(1);
				}
				
				BytesRead = fread(FanSettingString, sizeof(char), 32, GPUFanFile);
				
				if(BytesRead <= 0)
				{
					printf("Failed to read a fan sysfs entry for GPU %d.\n", Config.GPUIdx);
					free(GPUHWMonDir);
					free(GPUFanSysFSFileName);
					fclose(GPUFanFile);
					return(1);
				}
				
				FanSettingString[BytesRead] = 0x00;
				FanSetting = strtoul(FanSettingString, NULL, 10);
				
				FanSetting = round(((double)FanSetting * 100.0) / (double)FanMaxSetting);
				
				printf("%d%%\n", FanSetting);
				fclose(GPUFanFile);
			}
			
			free(GPUFanSysFSFileName);
		}
		free(GPUHWMonDir);
	}
	
	return(0);
}	
