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
 *  tssbridge.cpp
 *
 *  Author: John Lyle
 *
 *  This is a node.js module capable of communicating with attestation.c
 *  and therefore interfacing the with a Trusted Platform Module via
 *  the Trusted Software Stack.
 *
 *  Requirements are listed in attestation.c, but essentially the TrouSerS
 *  library, a TPM and OpenSSL.
 *
 */

#include <v8.h>
#include <node.h>
#include <iostream>
#include "v8-convert.hpp"

#include "convert.hpp" 
#include "invocable.hpp" 
#include "properties.hpp"
#include "arguments.hpp"
#include "XTo.hpp" 
 

extern "C" {
#include "tsscommands.h"
}

namespace cv = cvv8;
using namespace node;
using namespace v8;

/*
 * Wrap the "pcrRead" function to marshal and unmarshall arguments.
 */
static Handle<Value> getPCR(const Arguments& args) {
    if (args.Length() == 1 && args[0]->IsNumber()) {
	    int pcr = cv::CastFromJS<int>(args[0]);
	    BYTE* pcrRes = (BYTE*) calloc(20, sizeof(BYTE));

	    UINT32 pcrLen = pcrRead(pcr, &pcrRes);    
	    
	    if ((int)pcrLen < 0) {
		    return ThrowException(
				    Exception::Error(String::New("Could not read PCR")));
	    }
	    v8::Handle<v8::Array> rv = v8::Array::New(pcrLen);	    
	    for (UINT32 i = 0; i < pcrLen; i++) {
		    rv->Set( i, cv::CastToJS<BYTE>( pcrRes[i]) );
	    }    
	    free(pcrRes);
	    return rv;
    }
    return ThrowException(
	Exception::Error(String::New("invalid argument")));
}

static Handle<Value> extendPCR(const Arguments& args) {
    if (args.Length() == 2 && args[0]->IsNumber() ) {
	    int pcr = cv::CastFromJS<int>(args[0]);
        std::list<int> pcrData = cv::CastFromJS<std::list<int> >(args[1]);
        
        UINT32 length = pcrData.size();
        BYTE* input = (BYTE*) malloc(sizeof(BYTE) * length);
        std::copy(pcrData.begin(), pcrData.end(), input); 
        
        TSS_RESULT res = pcrExtend(pcr, length, input);
        
        if (res != TSS_SUCCESS) {
            return ThrowException(
				    Exception::Error(String::New("Could not extend PCR")));
        }
        
	    free(input);
	    
	    
	    return v8::Boolean::New(true);
    }
    return ThrowException(
	Exception::Error(String::New("invalid argument")));
}



static Local<Object> versionToObject(TPM_STRUCT_VER ver) {
	Local<Object> verObj = Object::New();
	verObj->Set(String::New("major"), Number::New(ver.major));
	verObj->Set(String::New("minor"), Number::New(ver.minor));
	verObj->Set(String::New("revMajor"), Number::New(ver.revMajor));
	verObj->Set(String::New("revMinor"), Number::New(ver.revMinor));
	return verObj;
}

static Local<Object> version2ToObject(TSS_VERSION ver) {
	Local<Object> verObj = Object::New();
	verObj->Set(String::New("bMajor"), Number::New(ver.bMajor));
	verObj->Set(String::New("bMinor"), Number::New(ver.bMinor));
	verObj->Set(String::New("bRevMajor"), Number::New(ver.bRevMajor));
	verObj->Set(String::New("bRevMinor"), Number::New(ver.bRevMinor));
	return verObj;
}

static Local<Array> bytesToArray(BYTE* array, long size) {
	Local<Array> arr = Array::New(size);
	for (int i = 0; i < size; i++) {
		arr->Set(i, Number::New(array[i]));
	}
	return arr;
}

static char* stringToChar(Local<String> str) {
	char* strChars = (char*) malloc(sizeof(char) * str->Length());
	str->WriteUtf8(strChars);
	return strChars;
}

/*
 * Wrap the quote() method to marshal and unmarshal arguments
 *
 * Entirely inefficient.
 *
 * Will throw an exception if the Quote fails.
 * Currently doing NO INPUT VALIDATION
 *
 * Arguments: srkpwd, aikfile, pcrs[], nonce
 *
 */
static Handle<Value> getQuote(const Arguments& args) {
	//unmarshal the arguments
	Local<String> srkpwd = args[0]->ToString();
	Local<String> aik = args[1]->ToString();
	Local<Object> pcrsObj = args[2]->ToObject();
	Local<Object> nonceObj = args[3]->ToObject();

	int fieldCount = 0;
	while (pcrsObj->Has(fieldCount))
		fieldCount++;
	long pcrs[fieldCount];
	int i = 0;
	for (i = 0; i < fieldCount; i++) {
		pcrs[i] = pcrsObj->Get(i)->Int32Value();
	}

	int j = 0;

	BYTE nonce[20];
	while (nonceObj->Has(j) && j < 20) {
		nonce[j] = nonceObj->Get(j)->Int32Value();
		j++;
	}

	char* srkpwdAscii = stringToChar(srkpwd);
	char* aikAscii = stringToChar(aik);

	TSS_VALIDATION valid;
	TPM_QUOTE_INFO quoteInfo;

	//perform the quote
	TSS_RESULT quoteRes = quote(srkpwdAscii, aikAscii, pcrs, fieldCount, nonce,
			&valid, &quoteInfo);
	if (quoteRes != 0) {
		return ThrowException(
				Exception::Error(String::New("Error producing TPM Quote")));
	}

	// turn all these stupid TSS structs into JSON structures!
	Local<Object> validData = Object::New();

	validData->Set(String::New("rgbData"),
			bytesToArray(valid.rgbData, valid.ulDataLength));
	validData->Set(String::New("rgbExternalData"),
			bytesToArray(valid.rgbExternalData, valid.ulExternalDataLength));
	validData->Set(String::New("rgbValidationData"),
			bytesToArray(valid.rgbValidationData, valid.ulValidationDataLength));
	validData->Set(String::New("versionInfo"),
			version2ToObject(valid.versionInfo));

	Local<Object> quoteData = Object::New();

	quoteData->Set(String::New("compositeHash"),
			bytesToArray(quoteInfo.compositeHash.digest, 20));
	quoteData->Set(String::New("externalData"),
			bytesToArray(quoteInfo.externalData.nonce, 20));
	quoteData->Set(String::New("fixed"), bytesToArray(quoteInfo.fixed, 4));
	quoteData->Set(String::New("versionInfo"),
			versionToObject(quoteInfo.version));

	Local<Object> both = Object::New();
	both->Set(String::New("validationData"), validData);
	both->Set(String::New("quoteInfo"), quoteData);

	free(aikAscii);
	free(srkpwdAscii);

	return both;
}

extern "C" {
static void init(Handle<Object> target) {
	NODE_SET_METHOD(target, "getPCR", getPCR);
	NODE_SET_METHOD(target, "getQuote", getQuote);
	NODE_SET_METHOD(target, "extendPCR", extendPCR);
}
NODE_MODULE(tssbridge, init)
;
}
