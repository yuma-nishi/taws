/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

package main

/*
#cgo CFLAGS: -I${SRCDIR}/../../App/inc
#cgo CFLAGS: -I${SRCDIR}/../../common
#include <stdlib.h>
#include "attester_api.h"
*/
import "C"

import (
	"errors"
	"unsafe"
)

type Attester struct{}

type TeepSessionResult int32

const (
	TeepSessionResultOK                = TeepSessionResult(C.TEEP_SESSION_RESULT_OK)
	TeepSessionResultTeepErrorResponse = TeepSessionResult(C.TEEP_SESSION_RESULT_TEEP_ERROR_RESPONSE)
	TeepSessionResultFatal             = TeepSessionResult(C.TEEP_SESSION_RESULT_FATAL)
	TeepSessionResultHTTPError         = TeepSessionResult(C.TEEP_SESSION_RESULT_HTTP_ERROR)
	TeepSessionResultOKDeviceActivated = TeepSessionResult(C.TEEP_SESSION_RESULT_OK_DEVICE_ACTIVATED)
)

func (r TeepSessionResult) Error() string {
	switch r {
	case TeepSessionResultTeepErrorResponse:
		return "teep error response"
	case TeepSessionResultFatal:
		return "fatal error"
	case TeepSessionResultHTTPError:
		return "http error"
	default:
		return "unknown teep session result"
	}
}

func (a *Attester) InitializeEnclave(keygen string) error {
	var cKeygen *C.char
	if keygen != "" {
		cKeygen = C.CString(keygen)
		defer C.free(unsafe.Pointer(cKeygen))
	}
	if ret := C.attester_init(cKeygen); ret != 0 {
		return errors.New("attester_init failed")
	}
	return nil
}

func (a *Attester) RunInstallSession(tamURL, wappName string) (TeepSessionResult, error) {
	cURL := C.CString(tamURL)
	cApp := C.CString(wappName)
	defer C.free(unsafe.Pointer(cURL))
	defer C.free(unsafe.Pointer(cApp))

	result := TeepSessionResult(C.attester_install(cURL, cApp))
	switch result {
	case TeepSessionResultOK, TeepSessionResultOKDeviceActivated:
		return result, nil
	case TeepSessionResultTeepErrorResponse, TeepSessionResultFatal, TeepSessionResultHTTPError:
		return result, result
	default:
		return result, errors.New("attester_install returned unknown result")
	}
}

func (a *Attester) InvokeWasm(wappName, funcName string, input []byte, maxOutput int) ([]byte, error) {
	if maxOutput <= 0 {
		return nil, errors.New("maxOutput must be > 0")
	}
	cWapp := C.CString(wappName)
	cFunc := C.CString(funcName)
	defer C.free(unsafe.Pointer(cWapp))
	defer C.free(unsafe.Pointer(cFunc))

	var inPtr *C.uint8_t
	var inLen C.size_t
	if len(input) > 0 {
		buf := C.CBytes(input)
		defer C.free(buf)
		inPtr = (*C.uint8_t)(buf)
		inLen = C.size_t(len(input))
	}

	out := make([]byte, maxOutput)
	var actual C.size_t
	ret := C.attester_invoke_wasm(
		cWapp,
		cFunc,
		inPtr,
		inLen,
		(*C.uint8_t)(unsafe.Pointer(&out[0])),
		C.size_t(len(out)),
		&actual,
	)
	if ret != 0 {
		return nil, errors.New("attester_invoke_wasm failed")
	}

	return out[:int(actual)], nil
}

func (a *Attester) Close() {
	C.attester_close()
}
