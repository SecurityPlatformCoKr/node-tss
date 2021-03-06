/*******************************************************************************
*  Code originally contributed to the webinos project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* 
*     http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* Copyright 2011 University of Oxford
*******************************************************************************/


/*
 * tsscommands.c
 *
 *  Created on: 25 Oct 2011
 *  Author: John Lyle
 *
 *
 */

#include "tsscommands.h"
#include <stdio.h>

#define NULL_HOBJECT 0
#define NULL_HKEY NULL_HOBJECT
#define NULL_HPCRS NULL_HOBJECT

char* getErrorCode(TSS_RESULT result) {
	if (result != 0) {
		char* errStr = (char*) Trspi_Error_String(result);
		return errStr;
	} else {
		return "";
	}
}


TSS_RESULT closeContext(TSS_HCONTEXT context) {
	TSS_RESULT result = Tspi_Context_FreeMemory(context, NULL);
	if (result != 0)
		return result;
	return Tspi_Context_Close(context);
}

TSS_RESULT connectContext(TSS_HCONTEXT context) {
	TSS_RESULT result = Tspi_Context_Connect(context, NULL);
	return result;
}

TSS_RESULT loadKeyByUUID(TSS_HCONTEXT context, TSS_UUID id, TSS_HKEY * key) {
	TSS_FLAG storeType = TSS_PS_TYPE_USER;
	return Tspi_Context_LoadKeyByUUID(context, storeType, id, key);
}


TSS_RESULT getSrk(TSS_HCONTEXT context, UINT32 secretMode, char* secret,
		UINT32 secretLen, TSS_HKEY* key) {
	TSS_HPOLICY srkPolicy;
	TSS_UUID SRK_UUID = TSS_UUID_SRK;
	TSS_RESULT result = Tspi_Context_LoadKeyByUUID(context, TSS_PS_TYPE_SYSTEM,
			SRK_UUID, key);
	if (result != 0)
		return result;
	result = Tspi_GetPolicyObject(*key, TSS_POLICY_USAGE, &srkPolicy);
	if (result != 0)
		return result;
	return Tspi_Policy_SetSecret(srkPolicy, secretMode, secretLen, (BYTE *)secret);
}

TSS_RESULT pcrExtend(UINT32 pcrNumber, UINT32 pcrDataLength, BYTE* data) 
{
    TSS_HCONTEXT context;
	TSS_RESULT result;
	TSS_HTPM tpm;
	UINT32 pcrResLength;
	BYTE* pcrRes;

	result = Tspi_Context_Create(&context);
	if (result != 0)
		return -1;
	result = connectContext(context);
	if (result != 0)
		return -1;
	result = Tspi_Context_GetTpmObject(context, &tpm);
	if (result != 0)
		return -1;
		
    result = Tspi_TPM_PcrExtend(tpm, pcrNumber, pcrDataLength, data, NULL, &pcrResLength, &pcrRes);
    if (result != 0) 
        return -1;
        
    return result;
}


TSS_RESULT getKeyFromFile(TSS_HCONTEXT context, TSS_HKEY srk, char* filename,
		TSS_HKEY* key) {
	FILE *file;
	BYTE *buf;
	UINT32 bufLen;
	TSS_RESULT result;

	/* Open the key file */
	if ((file = fopen(filename, "rb")) == NULL) {
		fprintf(stderr, "Unable to open the file %s\n", filename);
		return TSS_E_FAIL;
	}
	/* How long is it? */
	fseek(file, 0, SEEK_END);
	bufLen = ftell(file);

	/* Read the file */
	fseek(file, 0, SEEK_SET);
	buf = malloc(bufLen);
	if (fread(buf, 1, bufLen, file) != bufLen) {
		fprintf(stderr, "Unable to read file %s\n", filename);
		fclose(file);
		return TSS_E_FAIL;
	}
	fclose(file);
	/* Load by blob */
	result = Tspi_Context_LoadKeyByBlob(context, srk, bufLen, buf, key);
	free(buf);
	return result;

	/* Assign a policy, if necessary
	 * ... actually, I'm going to assume no policy / secret is required for an AIK.
	 */
	/*
	 if (pass) {
	 result = Tspi_Context_CreateObject(hContext, TSS_OBJECT_TYPE_POLICY,
	 TSS_POLICY_USAGE, &hAIKPolicy); CKERR;
	 result = Tspi_Policy_AssignToObject(hAIKPolicy, hAIK);
	 result = Tspi_Policy_SetSecret (hAIKPolicy, TSS_SECRET_MODE_PLAIN,
	 strlen(pass), pass); CKERR;
	 }
	 */

}


TSS_FLAG getSigningKeyFlags()
{
    TSS_FLAG keyFlags = TSS_KEYUSAGE_SIGN | TSS_KEY_SIZE_2048 | TSS_KEY_VOLATILE | TSS_KEY_NOT_MIGRATABLE;
    return keyFlags;
}

TSS_FLAG getStorageKeyFlags()
{
    /* With thanks to the TPM Tools examples!*/
    TSS_FLAG keyFlags = TSS_KEY_TYPE_STORAGE | TSS_KEY_SIZE_2048 | TSS_KEY_VOLATILE | TSS_KEY_AUTHORIZATION |
	    TSS_KEY_NOT_MIGRATABLE;
    return keyFlags;
}

TSS_RESULT createTpmKey2(TSS_HCONTEXT context, TSS_FLAG keyFlags, TSS_HKEY srk,
		TSS_HKEY * key) {
	TSS_RESULT result;
	TSS_HPOLICY keyPolicy;
	result = Tspi_Context_CreateObject(context, TSS_OBJECT_TYPE_RSAKEY, keyFlags, key);
	if (result != 0)
		return result;
	result = Tspi_Context_CreateObject(context, TSS_OBJECT_TYPE_POLICY, TSS_POLICY_USAGE,
			&keyPolicy);
	if (result != 0)
		return result;
	result = Tspi_Policy_SetSecret(keyPolicy, TSS_SECRET_MODE_NONE, 0, NULL);
	if (result != 0)
		return result;
	result = Tspi_Policy_AssignToObject(keyPolicy, *key);
	if (result != 0)
		return result;
	result = Tspi_Key_CreateKey(*key, srk, NULL_HPCRS);
	return result;
}


TSS_RESULT createKey(TSS_HKEY * key) 
{
    return TSS_E_FAIL;    
}


/*
 * Read a pcr with the given number, and store it into the given BYTE array.
 * Returns either  -1 on error, or the length of the pcr byte array.
 */
UINT32 pcrRead(int pcrNumber, BYTE** pcrRes) {
	TSS_HCONTEXT context;
	TSS_RESULT result;
	TSS_HTPM tpm;
	UINT32 pcrResLength;

	result = Tspi_Context_Create(&context);
	if (result != 0)
		return -1;
	result = connectContext(context);
	if (result != 0)
		return -1;
	result = Tspi_Context_GetTpmObject(context, &tpm);
	if (result != 0)
		return -1;
	result = Tspi_TPM_PcrRead(tpm, pcrNumber, &pcrResLength, pcrRes);
	if (result != 0) {
		printf("Could not read PCR %d\n", pcrNumber);
		return -1;
	}
	return pcrResLength;
}
/*

 * Creates a TPM quote and stores the output into "valid" and "quoteInfo"
 * Returns either 0 (success) or another value on error.

 *
 * Needs an SRK Password char* array, a filename, a list of PCR indexs,
 * the length of the PCR index, and a 20-byte nonce.
 *

 */
TSS_RESULT quote(char* srkPwd, char* aikfile, long pcrs[], int npcrs,
		BYTE nonce[], TSS_VALIDATION* valid, TPM_QUOTE_INFO* quoteInfo) {
	TSS_HCONTEXT context;
	TSS_RESULT result;
	TSS_HTPM tpm;
	TSS_HKEY srk;
	TSS_HKEY aik;

	result = Tspi_Context_Create(&context);
	if (result != 0)
		return result;
	result = connectContext(context);
	if (result != 0)
		return result;
	result = Tspi_Context_GetTpmObject(context, &tpm);
	if (result != 0)
		return result;
	result = getSrk(context, TSS_SECRET_MODE_PLAIN, srkPwd, strlen(srkPwd),
			&srk);
	if (result != 0)
		return result;
	result = getKeyFromFile(context, srk, aikfile, &aik);
	if (result != 0)
		return result;
	result = createQuote(pcrs, 1, nonce, context, tpm, srk, aik, valid,
			quoteInfo);
	if (result != 0)
		return result;
	result = closeContext(context);
	if (result != 0)
		return result;

	return result;
}

/* The following method has this copyright notice */
/*
 * Copyright (c) 2009 Hal Finney
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* nonce is 20 in length */
TSS_RESULT createQuote(long pcrs[], UINT32 npcrs, BYTE nonce[],
		TSS_HCONTEXT context, TSS_HTPM tpm, TSS_HKEY srk, TSS_HKEY aik,
		TSS_VALIDATION* valid, TPM_QUOTE_INFO* quoteInfo) {
	UINT32 tpmProp;
	UINT32 npcrMax;
	UINT32 npcrBytes;
	TSS_HPCRS hPCRs;
	BYTE *tmpbuf;
	UINT32 tmpbufLen;
	BYTE *buf;
	UINT32 i;
//	UINT32 bufLen;
	BYTE *bp;

	/* Create PCR list to be quoted */
	tpmProp = TSS_TPMCAP_PROP_PCR;
	TSS_RESULT result = Tspi_TPM_GetCapability(tpm, TSS_TPMCAP_PROPERTY,
			sizeof(tpmProp), (BYTE *) &tpmProp, &tmpbufLen, &tmpbuf);
	if (result != 0)
		return result;

	npcrMax = *(UINT32 *) tmpbuf;
	result = Tspi_Context_FreeMemory(context, tmpbuf);
	if (result != 0)
		return result;
	npcrBytes = (npcrMax + 7) / 8;
	result = Tspi_Context_CreateObject(context, TSS_OBJECT_TYPE_PCRS,
			TSS_PCRS_STRUCT_INFO, &hPCRs);
	if (result != 0)
		return result;

	/* Also PCR buffer */
	buf = malloc(2 + npcrBytes + 4 + 20 * npcrMax);
	*(UINT16 *) buf = htons(npcrBytes);
	for (i = 0; i < npcrBytes; i++)
		buf[2 + i] = 0;

	for (i = 0; i < npcrs; i++) {
		result = Tspi_PcrComposite_SelectPcrIndex(hPCRs, pcrs[i]);
		if (result != 0)
			return result;
		buf[2 + (pcrs[i] / 8)] |= 1 << (pcrs[i] % 8);
	}

	/* Create TSS_VALIDATION struct for Quote */
	(*valid).ulExternalDataLength = 20;
	(*valid).rgbExternalData = nonce;

	/* Perform Quote */
	result = Tspi_TPM_Quote(tpm, aik, hPCRs, valid);
	if (result != 0)
		return result;
	if (quoteInfo) {
	    memcpy(quoteInfo, (*valid).rgbData, sizeof(TPM_QUOTE_INFO));
	}
	/* Fill in rest of PCR buffer */
	bp = buf + 2 + npcrBytes;
	*(UINT32 *) bp = htonl(20 * npcrs);
	bp += sizeof(UINT32);
	for (i = 0; i < npcrMax; i++) {
		if (buf[2 + (i / 8)] & (1 << (i % 8))) {
			result = Tspi_PcrComposite_GetPcrValue(hPCRs, i, &tmpbufLen,
					&tmpbuf);
			if (result != 0)
				return result;
			memcpy(bp, tmpbuf, tmpbufLen);
			bp += tmpbufLen;
			result = Tspi_Context_FreeMemory(context, tmpbuf);
			if (result != 0)
				return result;
		}
	}
//	bufLen = bp - buf;
	Tspi_Context_FreeMemory(context, tmpbuf);
	return result;
}

