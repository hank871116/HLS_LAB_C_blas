/**********
 * Copyright 2019 Xilinx, Inc.
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
 * **********/

#include <stdio.h>
#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <iomanip>

#include "fpga.hpp"

using namespace std;

ifstream::pos_type getFileSize(string p_FileName) {
    ifstream in(p_FileName.c_str(), ifstream::ate | ifstream::binary);
    return in.tellg();
}

vector<uint8_t> loadBinFile(string p_BinFileName) {
    vector<uint8_t> l_memVec;
    // Bin file existence
    ifstream l_if(p_BinFileName.c_str(), ios::binary);
    if (l_if.is_open()) {
        // Bin file size
        size_t l_binFileSize = getFileSize(p_BinFileName);
        cout << "INFO: loading " + p_BinFileName + " of size " << l_binFileSize << "\n";
        assert(l_binFileSize > 0);

        // Bin file storage
        // l_memVec.reserve(l_binFileSizeInDdrWords);
        l_memVec.resize(l_binFileSize);
        uint8_t* l_mem = &l_memVec[0];

        // Read the bin file
        l_if.read((char*)l_mem, l_binFileSize);
        if (l_if) {
            cout << "INFO: loaded " << l_binFileSize << " bytes from " << p_BinFileName << "\n";
        } else {
            l_memVec.clear();
            cout << "ERROR: loaded only " << l_if.gcount() << " bytes from " << p_BinFileName << "\n";
        }
        l_if.close();

        // Debug print the file content
    } else {
        cout << "ERROR: failed to open file " + p_BinFileName + "\n";
    }

    return (l_memVec);
}

bool writeBinFile(string p_BinFileName, vector<uint8_t>& p_MemVec) {
    bool ok = false;
    ofstream l_of(p_BinFileName.c_str(), ios::binary);
    if (l_of.is_open()) {
        size_t l_sizeBytes = p_MemVec.size();
        l_of.write((char*)&p_MemVec[0], l_sizeBytes);
        if (l_of.tellp() == (unsigned)l_sizeBytes) {
            cout << "INFO: wrote " << l_sizeBytes << " bytes to " << p_BinFileName << "\n";
            ok = true;
        } else {
            cout << "ERROR: wrote only " << l_of.tellp() << " bytes to " << p_BinFileName << "\n";
        }
        l_of.close();
    }
    return (ok);
}

typedef chrono::time_point<chrono::high_resolution_clock> TimePointType;

void showTimeData(string p_Task, TimePointType& t1, TimePointType& t2) {
    t2 = chrono::high_resolution_clock::now();
    chrono::duration<double> l_durationSec = t2 - t1;
    cout << "  DATA: time " << p_Task << "  " << fixed << setprecision(6) << l_durationSec.count() * 1e3 << " msec\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("ERROR: passed %d arguments instead of %d, exiting\n", argc, 3);
        printf("  Usage:\n    blas_host.exe  blas.xclbin  data/ [deviceID]\n");
        return EXIT_FAILURE;
    }

    string l_xclbinFile(argv[1]);
    string l_binFile(argv[2]);
    l_binFile = l_binFile + string("/app.bin");
    string l_binFileOut(argv[2]);
    l_binFileOut = l_binFileOut + string("/app_out.bin");
    int l_deviceID = 0;
    if (argc == 4) l_deviceID = atoi(argv[3]);

    printf("GEMX:   %s  %s  %s %s\n", argv[0], l_xclbinFile.c_str(), l_binFile.c_str(), l_binFileOut.c_str());

    // Load the bin file
    vector<uint8_t> l_memVec[BLAS_numKernels];

    for (unsigned int i = 0; i < BLAS_numKernels; ++i) {
        l_memVec[i] = loadBinFile(l_binFile);

        if (l_memVec[i].empty()) {
            return EXIT_FAILURE;
        }
    }

#include <chrono>
    TimePointType l_tp[10];
    unsigned int l_tpIdx = 0;
    l_tp[l_tpIdx] = chrono::high_resolution_clock::now();

    xf::blas::Fpga l_fpga(l_deviceID);

    if (l_fpga.loadXclbin(l_xclbinFile)) {
        cout << "INFO: created kernels" << endl;
    } else {
        cerr << "ERROR: failed to load " + l_xclbinFile + "\n";
        return EXIT_FAILURE;
    }
    showTimeData("loadXclbin", l_tp[l_tpIdx], l_tp[l_tpIdx + 1]);
    l_tpIdx++;

    for (unsigned int i = 0; i < BLAS_numKernels; ++i) {
        string l_krnName = "gemmLoadStoreKernel";
        if (!l_fpga.createKernel(i, l_krnName)) {
            cerr << "ERROR: failed to create kernel " << i << " " << endl;
        }
    }
    showTimeData("create kernels", l_tp[l_tpIdx], l_tp[l_tpIdx + 1]);
    l_tpIdx++;

    xf::blas::MemDesc l_memDesc[BLAS_numKernels];
    for (unsigned int i = 0; i < BLAS_numKernels; ++i) {
        l_memDesc[i].init(l_memVec[i].size() / BLAS_pageSizeBytes, l_memVec[i].data());
        assert(l_memVec[i].size() % BLAS_pageSizeBytes == 0);
        if (!l_fpga.createBufferForKernel(i, l_memDesc[i])) {
            cerr << "ERROR: failed to create buffer for kernel " << i << endl;
        }
    }
    showTimeData("create buffers", l_tp[l_tpIdx], l_tp[l_tpIdx + 1]);
    l_tpIdx++;

    // Transfer data to FPGA
    for (unsigned int i = 0; i < BLAS_numKernels; ++i) {
        if (l_fpga.copyToKernel(i)) {
            cout << "INFO: transferred data to kernel " << i << endl;
        } else {
            cerr << "ERROR: failed to transfer data to kernel" << i << endl;
            return EXIT_FAILURE;
        }
    }
    showTimeData("copy to kernels", l_tp[l_tpIdx], l_tp[l_tpIdx + 1]);
    l_tpIdx++;

    // launch kernels
    for (unsigned int i = 0; i < BLAS_numKernels; ++i) {
        if (l_fpga.callKernel(i)) {
            cout << "INFO: Executed kernel " << i << endl;
        } else {
            cerr << "ERROR: failed to call kernel " << i << endl;
            return EXIT_FAILURE;
        }
    }
    showTimeData("call kernels", l_tp[l_tpIdx], l_tp[l_tpIdx + 1]);
    l_tpIdx++;
    l_fpga.finish();

    // Transfer data back to host
    for (unsigned int i = 0; i < BLAS_numKernels; ++i) {
        if (l_fpga.copyFromKernel(i)) {
            cout << "INFO: Transferred data from kernel" << i << endl;
        } else {
            cerr << "ERROR: failed to transfer data from kernel " << i << endl;
            return EXIT_FAILURE;
        }
    }
    l_fpga.finish();
    showTimeData("copyFromFpga", l_tp[l_tpIdx], l_tp[l_tpIdx + 1]);
    l_tpIdx++;
    showTimeData("total", l_tp[0], l_tp[l_tpIdx]);
    l_tpIdx++;
    showTimeData("subtotalFpga", l_tp[1], l_tp[l_tpIdx]);
    l_tpIdx++; // Host->DDR, kernel, DDR->host

    // Write out the received data
    for (int i = 0; i < BLAS_numKernels; ++i) {
        size_t pos1 = l_binFileOut.find("app_out");
        size_t pos2 = l_binFileOut.find(".bin");
        size_t size_pos = pos2 - pos1;

        string binFileOutName =
            argv[2] + string("/") + l_binFileOut.substr(pos1, size_pos) + to_string(i) + l_binFileOut.substr(pos2, 4);
        if (!writeBinFile(binFileOutName, l_memVec[i])) {
            cerr << "ERROR: failed to write output file " + binFileOutName + "\n";
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
