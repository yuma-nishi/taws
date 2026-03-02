/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

package main

import (
	"embed"
	"html/template"
	"io"
	"log"
	"net/http"
	"runtime"
	"sync"
)

type serverConfig struct {
	addr     string
	wappName string
	funcName string
	maxOut   int
	keygen   string
	tamURL   string
}

type detectorServer struct {
	cfg   serverConfig
	reqCh chan detectRequest
	once  sync.Once
	mu    sync.Mutex
}

type detectRequest struct {
	input []byte
	resp  chan detectResponse
}

type detectResponse struct {
	output []byte
	err    error
}

func (s *detectorServer) init() error {
	s.once.Do(func() {
		s.reqCh = make(chan detectRequest)
		go s.runWorker()
	})
	return nil
}

func (s *detectorServer) runWorker() {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()

	att := &Attester{}
	if err := att.InitializeEnclave(s.cfg.keygen); err != nil {
		for req := range s.reqCh {
			req.resp <- detectResponse{err: err}
		}
		return
	}
	defer att.Close()

	for req := range s.reqCh {
		s.mu.Lock()
		output, err := att.InvokeWasm(s.cfg.wappName, s.cfg.funcName, req.input, s.cfg.maxOut)
		s.mu.Unlock()
		req.resp <- detectResponse{output: output, err: err}
	}
}

func (s *detectorServer) handleIndex(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	if err := webTemplate.Execute(w, nil); err != nil {
		log.Printf("render template failed: %v", err)
	}
}

func (s *detectorServer) handleDetect(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if err := s.init(); err != nil {
		http.Error(w, "init failed", http.StatusInternalServerError)
		return
	}

	if err := r.ParseMultipartForm(16 << 20); err != nil {
		http.Error(w, "invalid multipart form", http.StatusBadRequest)
		return
	}
	file, _, err := r.FormFile("image")
	if err != nil {
		http.Error(w, "missing image", http.StatusBadRequest)
		return
	}
	defer file.Close()

	inputBytes, err := io.ReadAll(file)
	if err != nil {
		http.Error(w, "read image failed", http.StatusBadRequest)
		return
	}
	if len(inputBytes) == 0 {
		http.Error(w, "empty image", http.StatusBadRequest)
		return
	}

	respCh := make(chan detectResponse, 1)
	s.reqCh <- detectRequest{input: inputBytes, resp: respCh}
	resp := <-respCh
	if resp.err != nil {
		http.Error(w, "invoke detector failed", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "image/jpeg")
	w.WriteHeader(http.StatusOK)
	if _, err := w.Write(resp.output); err != nil {
		log.Printf("write response failed: %v", err)
	}
}

func (s *detectorServer) handleTEEP(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	att := &Attester{}
	if err := att.InitializeEnclave(s.cfg.keygen); err != nil {
		http.Error(w, "install failed", http.StatusInternalServerError)
		return
	}
	result, err := att.RunInstallSession(s.cfg.tamURL, s.cfg.wappName)
	if err != nil {
		http.Error(w, "install failed", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	if result == TeepSessionResultOKDeviceActivated {
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte("The device has been activated. You can install the app."))
		return
	}
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write([]byte("TEEP install finished."))
}

func runDetectorWeb(cfg serverConfig) {
	server := &detectorServer{cfg: cfg}

	mux := http.NewServeMux()
	mux.HandleFunc("/", server.handleIndex)
	mux.HandleFunc("/detect", server.handleDetect)
	mux.HandleFunc("/teep", server.handleTEEP)

	log.Printf("attester web UI: http://%s", cfg.addr)
	if err := http.ListenAndServe(cfg.addr, mux); err != nil {
		log.Fatalf("server failed: %v", err)
	}
}

//go:embed web_template.html
var webTemplateFS embed.FS

var webTemplate = template.Must(template.ParseFS(webTemplateFS, "web_template.html"))
