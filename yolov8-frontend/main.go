/*
 * Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

package main

import (
	"bufio"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"strings"
)

const (
	defaultTamURL   = "http://localhost:8080/tam"
	defaultKeygen   = "yes"
	defaultWappName = "yolov8.wasm"
	defaultFuncName = "detector_yolov8_wasm"
	defaultAddr     = "127.0.0.1:8181"
	defaultMaxOut   = 16 << 20
)

func main() {
	if len(os.Args) < 2 {
		printUsage()
		os.Exit(1)
	}

	sub := os.Args[1]
	switch sub {
	case "web":
		runWeb(os.Args[2:])
	case "cli":
		runCLI(os.Args[2:])
	default:
		printUsage()
		os.Exit(1)
	}
}

func runWeb(args []string) {
	fs := flag.NewFlagSet("web", flag.ExitOnError)
	addr := fs.String("addr", defaultAddr, "listen address")
	wapp := fs.String("wapp", defaultWappName, "WASM app name")
	funcName := fs.String("func", defaultFuncName, "exported WASM function")
	maxOut := fs.Int("max-output", defaultMaxOut, "max output bytes")
	keygen := fs.String("keygen", defaultKeygen, "keygen mode: yes|no")
	url := fs.String("url", defaultTamURL, "TAM URL")
	_ = fs.Parse(args)

	runDetectorWeb(serverConfig{
		addr:     *addr,
		wappName: *wapp,
		funcName: *funcName,
		maxOut:   *maxOut,
		keygen:   *keygen,
		tamURL:   *url,
	})
}

func runCLI(args []string) {
	fs := flag.NewFlagSet("cli", flag.ExitOnError)
	keygen := fs.String("keygen", defaultKeygen, "keygen mode: yes|no")
	_ = fs.Parse(args)

	att := &Attester{}
	if err := att.InitializeEnclave(*keygen); err != nil {
		fatal(err)
	}
	defer att.Close()

	log.Println("attester cli mode (init once). commands: install, detector, help, exit")

	scanner := bufio.NewScanner(os.Stdin)
	for {
		fmt.Print("> ")
		if !scanner.Scan() {
			break
		}
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		fields := strings.Fields(line)
		cmd := fields[0]
		cmdArgs := fields[1:]

		switch cmd {
		case "install":
			if err := runInstallSession(att, cmdArgs); err != nil {
				fmt.Fprintln(os.Stderr, err)
			}
		case "detector":
			if err := runDetectorSession(att, cmdArgs); err != nil {
				fmt.Fprintln(os.Stderr, err)
			}
		case "help":
			printCLIUsage()
		case "exit", "quit":
			return
		default:
			fmt.Fprintln(os.Stderr, "unknown command:", cmd)
			printCLIUsage()
		}
	}
	if err := scanner.Err(); err != nil {
		fmt.Fprintln(os.Stderr, "read error:", err)
	}
}

func printUsage() {
	fmt.Fprintln(os.Stderr, `Usage:
  attester web [--addr ADDR] [--wapp NAME] [--func NAME] [--keygen yes|no] [--max-output BYTES] [--url URL]
  attester cli [--keygen yes|no]
`)
}

func printCLIUsage() {
	fmt.Fprintln(os.Stderr, `cli commands:
  install [--url URL] [--wapp NAME]
  detector [--wapp NAME] [--func NAME] [--max-output BYTES] <input.jpg> [output.jpg]
  help
  exit
`)
}

func runInstallSession(att *Attester, args []string) error {
	fs := flag.NewFlagSet("install", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	url := fs.String("url", defaultTamURL, "TAM URL")
	wapp := fs.String("wapp", defaultWappName, "WASM app name")
	if err := fs.Parse(args); err != nil {
		return errors.New("usage: install [--url URL] [--wapp NAME]")
	}
	result, err := att.RunInstallSession(*url, *wapp)
	if err != nil {
		return err
	}
	if result == TeepSessionResultOKDeviceActivated {
		log.Println("The device has been activated. You can install the app.")
		return nil
	}
	log.Println("TEEP install finished.")
	return nil
}

func runDetectorSession(att *Attester, args []string) error {
	fs := flag.NewFlagSet("detector", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	wapp := fs.String("wapp", defaultWappName, "WASM app name")
	funcName := fs.String("func", defaultFuncName, "exported WASM function")
	maxOut := fs.Int("max-output", defaultMaxOut, "max output bytes")
	if err := fs.Parse(args); err != nil {
		return errors.New("usage: detector [--wapp NAME] [--func NAME] [--max-output BYTES] <input.jpg> [output.jpg]")
	}

	if fs.NArg() < 1 {
		return errors.New("usage: detector <input.jpg> [output.jpg]")
	}
	inputPath := fs.Arg(0)
	outputPath := "detected.jpg"
	if fs.NArg() > 1 {
		outputPath = fs.Arg(1)
	}
	outputPath = filepath.Clean(outputPath)

	inputBytes, err := os.ReadFile(inputPath)
	if err != nil {
		return err
	}
	if len(inputBytes) == 0 {
		return errors.New("input is empty")
	}

	output, err := att.InvokeWasm(*wapp, *funcName, inputBytes, *maxOut)
	if err != nil {
		return err
	}
	if err := os.WriteFile(outputPath, output, 0644); err != nil {
		return err
	}
	log.Printf("wrote %s", outputPath)
	return nil
}

func fatal(err error) {
	fmt.Fprintln(os.Stderr, err)
	os.Exit(1)
}
