/*
 * Copyright 2011, Blender Foundation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor: 
 *		Jeroen Bakker 
 *		Monique Dewanchand
 */

#include "COM_GammaOperation.h"
#include "BLI_math.h"

GammaOperation::GammaOperation(): NodeOperation() {
	this->addInputSocket(COM_DT_COLOR);
	this->addInputSocket(COM_DT_VALUE);
	this->addOutputSocket(COM_DT_COLOR);
	this->inputProgram = NULL;
	this->inputGammaProgram = NULL;
}
void GammaOperation::initExecution() {
	this->inputProgram = this->getInputSocketReader(0);
	this->inputGammaProgram = this->getInputSocketReader(1);
}

void GammaOperation::executePixel(float* color, float x, float y, PixelSampler sampler, MemoryBuffer *inputBuffers[]) {
	float inputValue[4];
	float inputGamma[4];
	
	this->inputProgram->read(inputValue, x, y, sampler, inputBuffers);
	this->inputGammaProgram->read(inputGamma, x, y, sampler, inputBuffers);
	const float gamma = inputGamma[0];
	/* check for negative to avoid nan's */
	color[0] = inputValue[0]>0.0f?pow(inputValue[0], gamma):inputValue[0];
	color[1] = inputValue[1]>0.0f?pow(inputValue[1], gamma):inputValue[1];
	color[2] = inputValue[2]>0.0f?pow(inputValue[2], gamma):inputValue[2];
	
	color[3] = inputValue[3];
}

void GammaOperation::deinitExecution() {
	this->inputProgram = NULL;
	this->inputGammaProgram = NULL;
}
