#!/bin/bash
gcovr --filter 'src/rpc/rpc_fw' build/debug/src build/debug/test
