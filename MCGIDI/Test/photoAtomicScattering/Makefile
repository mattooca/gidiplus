SHELL = /bin/ksh

# <<BEGIN-copyright>>
# Copyright 2019, Lawrence Livermore National Security, LLC.
# See the top-level COPYRIGHT file for details.
# 
# SPDX-License-Identifier: MIT
# <<END-copyright>>

GIDI_PLUS_PATH ?= $(abspath ../../..)
CppSource = $(sort $(wildcard *.cpp))

include ../../Makefile.paths
include ../Makefile.check

check: sampleCoherent
	./sampleCoherent > o
#	../Utilities/diff.com sampleCoherent/sampleCoherent sampleCoherent.out o
