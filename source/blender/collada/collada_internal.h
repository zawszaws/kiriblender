#ifndef BLENDER_COLLADA_H
#define BLENDER_COLLADA_H

#include "COLLADAFWFileInfo.h"
#include "Math/COLLADABUMathMatrix4.h"

class UnitConverter
{
private:
	COLLADAFW::FileInfo::Unit unit;
	COLLADAFW::FileInfo::UpAxisType up_axis;

public:

	UnitConverter() : unit(), up_axis(COLLADAFW::FileInfo::Z_UP) {}

	void read_asset(const COLLADAFW::FileInfo* asset)
	{
	}

	// TODO
	// convert vector vec from COLLADA format to Blender
	void convertVec3(float *vec)
	{
	}
		
	// TODO need also for angle conversion, time conversion...

	void mat4_from_dae(float out[][4], const COLLADABU::Math::Matrix4& in)
	{
		// in DAE, matrices use columns vectors, (see comments in COLLADABUMathMatrix4.h)
		// so here, to make a blender matrix, we swap columns and rows
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				out[i][j] = in[j][i];
			}
		}
	}

	void mat4_to_dae(float out[][4], float in[][4])
	{
		Mat4CpyMat4(out, in);
		Mat4Transp(out);
	}

	void mat4_to_dae(double out[][4], float in[][4])
	{
		float outf[4][4];

		mat4_to_dae(outf, in);

		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				out[i][j] = outf[i][j];
	}
};

#endif
