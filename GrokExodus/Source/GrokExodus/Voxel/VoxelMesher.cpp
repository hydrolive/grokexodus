// Copyright Epic Games, Inc. All Rights Reserved.
// Marching Cubes implementation for planetary voxel crust.

#include "Voxel/VoxelMesher.h"
#include "Voxel/VoxelChunk.h"
#include "Voxel/VoxelSphereMapping.h"

namespace VoxelMC
{
	static FORCEINLINE int32 GridIndex(int32 X, int32 Y, int32 Z, int32 SX, int32 SY)
	{
		return X + SX * (Y + SY * Z);
	}

	// Corner offsets for cube (same bit layout as classic MC)
	static const FVector CornerOff[8] = {
		{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0},
		{0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}
	};

	// Edge endpoints (corner indices)
	static const int32 EdgeCorner[12][2] = {
		{0,1},{1,2},{2,3},{3,0},
		{4,5},{5,6},{6,7},{7,4},
		{0,4},{1,5},{2,6},{3,7}
	};

	// edgeTable[256] — which edges are intersected
	static const int32 EdgeTable[256] = {
		0x0  , 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
		0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
		0x190, 0x99 , 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
		0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
		0x230, 0x339, 0x33 , 0x13a, 0x636, 0x73f, 0x435, 0x53c,
		0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
		0x3a0, 0x2a9, 0x1a3, 0xaa , 0x7a6, 0x6af, 0x5a5, 0x4ac,
		0xbac, 0xaa5, 0x9af, 0x8a6, 0xfaa, 0xea3, 0xda9, 0xca0,
		0x460, 0x569, 0x663, 0x76a, 0x66 , 0x16f, 0x265, 0x36c,
		0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
		0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff , 0x3f5, 0x2fc,
		0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
		0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55 , 0x15c,
		0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
		0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc ,
		0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
		0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
		0xcc , 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
		0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
		0x15c, 0x55 , 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
		0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
		0x2fc, 0x3f5, 0xff , 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
		0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
		0x36c, 0x265, 0x16f, 0x66 , 0x76a, 0x663, 0x569, 0x460,
		0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac,
		0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa , 0x1a3, 0x2a9, 0x3a0,
		0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
		0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33 , 0x339, 0x230,
		0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
		0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99 , 0x190,
		0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
		0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0
	};

	// triTable[256][16] — edge indices for triangles, -1 terminated
	// Compact: only non-empty configs listed via full classic table
	static const int8 TriTable[256][16] = {
		{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,1,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,8,3,9,8,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,8,3,1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{9,2,10,0,2,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{2,8,3,2,10,8,10,9,8,-1,-1,-1,-1,-1,-1,-1},
		{3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,11,2,8,11,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,9,0,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,11,2,1,9,11,9,8,11,-1,-1,-1,-1,-1,-1,-1},
		{3,10,1,11,10,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,10,1,0,8,10,8,11,10,-1,-1,-1,-1,-1,-1,-1},
		{3,9,0,3,11,9,11,10,9,-1,-1,-1,-1,-1,-1,-1},
		{9,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{4,3,0,7,3,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,1,9,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{4,1,9,4,7,1,7,3,1,-1,-1,-1,-1,-1,-1,-1},
		{1,2,10,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{3,4,7,3,0,4,1,2,10,-1,-1,-1,-1,-1,-1,-1},
		{9,2,10,9,0,2,8,4,7,-1,-1,-1,-1,-1,-1,-1},
		{2,10,9,2,9,7,2,7,3,7,9,4,-1,-1,-1,-1},
		{8,4,7,3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{11,4,7,11,2,4,2,0,4,-1,-1,-1,-1,-1,-1,-1},
		{9,0,1,8,4,7,2,3,11,-1,-1,-1,-1,-1,-1,-1},
		{4,7,11,9,4,11,9,11,2,9,2,1,-1,-1,-1,-1},
		{3,10,1,3,11,10,7,8,4,-1,-1,-1,-1,-1,-1,-1},
		{1,11,10,1,4,11,1,0,4,7,11,4,-1,-1,-1,-1},
		{4,7,8,9,0,11,9,11,10,11,0,3,-1,-1,-1,-1},
		{4,7,11,4,11,9,9,11,10,-1,-1,-1,-1,-1,-1,-1},
		{9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{9,5,4,0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,5,4,1,5,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{8,5,4,8,3,5,3,1,5,-1,-1,-1,-1,-1,-1,-1},
		{1,2,10,9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{3,0,8,1,2,10,4,9,5,-1,-1,-1,-1,-1,-1,-1},
		{5,2,10,5,4,2,4,0,2,-1,-1,-1,-1,-1,-1,-1},
		{2,10,5,3,2,5,3,5,4,3,4,8,-1,-1,-1,-1},
		{9,5,4,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,11,2,0,8,11,4,9,5,-1,-1,-1,-1,-1,-1,-1},
		{0,5,4,0,1,5,2,3,11,-1,-1,-1,-1,-1,-1,-1},
		{2,1,5,2,5,8,2,8,11,4,8,5,-1,-1,-1,-1},
		{10,3,11,10,1,3,9,5,4,-1,-1,-1,-1,-1,-1,-1},
		{4,9,5,0,8,1,8,10,1,8,11,10,-1,-1,-1,-1},
		{5,4,0,5,0,11,5,11,10,11,0,3,-1,-1,-1,-1},
		{5,4,8,5,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1},
		{9,7,8,5,7,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{9,3,0,9,5,3,5,7,3,-1,-1,-1,-1,-1,-1,-1},
		{0,7,8,0,1,7,1,5,7,-1,-1,-1,-1,-1,-1,-1},
		{1,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{9,7,8,9,5,7,10,1,2,-1,-1,-1,-1,-1,-1,-1},
		{10,1,2,9,5,0,5,3,0,5,7,3,-1,-1,-1,-1},
		{8,0,2,8,2,5,8,5,7,10,5,2,-1,-1,-1,-1},
		{2,10,5,2,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1},
		{7,9,5,7,8,9,3,11,2,-1,-1,-1,-1,-1,-1,-1},
		{9,5,7,9,7,2,9,2,0,2,7,11,-1,-1,-1,-1},
		{2,3,11,0,1,8,1,7,8,1,5,7,-1,-1,-1,-1},
		{11,2,1,11,1,7,7,1,5,-1,-1,-1,-1,-1,-1,-1},
		{9,5,8,8,5,7,10,1,3,10,3,11,-1,-1,-1,-1},
		{5,7,0,5,0,9,7,11,0,1,0,10,11,10,0,-1},
		{11,10,0,11,0,3,10,5,0,8,0,7,5,7,0,-1},
		{11,10,5,7,11,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,8,3,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{9,0,1,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,8,3,1,9,8,5,10,6,-1,-1,-1,-1,-1,-1,-1},
		{1,6,5,2,6,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,6,5,1,2,6,3,0,8,-1,-1,-1,-1,-1,-1,-1},
		{9,6,5,9,0,6,0,2,6,-1,-1,-1,-1,-1,-1,-1},
		{5,9,8,5,8,2,5,2,6,3,2,8,-1,-1,-1,-1},
		{2,3,11,10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{11,0,8,11,2,0,10,6,5,-1,-1,-1,-1,-1,-1,-1},
		{0,1,9,2,3,11,5,10,6,-1,-1,-1,-1,-1,-1,-1},
		{5,10,6,1,9,2,9,11,2,9,8,11,-1,-1,-1,-1},
		{6,3,11,6,5,3,5,1,3,-1,-1,-1,-1,-1,-1,-1},
		{0,8,11,0,11,5,0,5,1,5,11,6,-1,-1,-1,-1},
		{3,11,6,0,3,6,0,6,5,0,5,9,-1,-1,-1,-1},
		{6,5,9,6,9,11,11,9,8,-1,-1,-1,-1,-1,-1,-1},
		{5,10,6,4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{4,3,0,4,7,3,6,5,10,-1,-1,-1,-1,-1,-1,-1},
		{1,9,0,5,10,6,8,4,7,-1,-1,-1,-1,-1,-1,-1},
		{10,6,5,1,9,7,1,7,3,7,9,4,-1,-1,-1,-1},
		{6,1,2,6,5,1,4,7,8,-1,-1,-1,-1,-1,-1,-1},
		{1,2,5,5,2,6,3,0,4,3,4,7,-1,-1,-1,-1},
		{8,4,7,9,0,5,0,6,5,0,2,6,-1,-1,-1,-1},
		{7,3,9,7,9,4,3,2,9,5,9,6,2,6,9,-1},
		{3,11,2,7,8,4,10,6,5,-1,-1,-1,-1,-1,-1,-1},
		{5,10,6,4,7,2,4,2,0,2,7,11,-1,-1,-1,-1},
		{0,1,9,4,7,8,2,3,11,5,10,6,-1,-1,-1,-1},
		{9,2,1,9,11,2,9,4,11,7,11,4,5,10,6,-1},
		{8,4,7,3,11,5,3,5,1,5,11,6,-1,-1,-1,-1},
		{5,1,11,5,11,6,1,0,11,7,11,4,0,4,11,-1},
		{0,5,9,0,6,5,0,3,6,11,6,3,8,4,7,-1},
		{6,5,9,6,9,11,4,7,9,7,11,9,-1,-1,-1,-1},
		{10,4,9,6,4,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{4,10,6,4,9,10,0,8,3,-1,-1,-1,-1,-1,-1,-1},
		{10,0,1,10,6,0,6,4,0,-1,-1,-1,-1,-1,-1,-1},
		{8,3,1,8,1,6,8,6,4,6,1,10,-1,-1,-1,-1},
		{1,4,9,1,2,4,2,6,4,-1,-1,-1,-1,-1,-1,-1},
		{3,0,8,1,2,9,2,4,9,2,6,4,-1,-1,-1,-1},
		{0,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{8,3,2,8,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1},
		{10,4,9,10,6,4,11,2,3,-1,-1,-1,-1,-1,-1,-1},
		{0,8,2,2,8,11,4,9,10,4,10,6,-1,-1,-1,-1},
		{3,11,2,0,1,6,0,6,4,6,1,10,-1,-1,-1,-1},
		{6,4,1,6,1,10,4,8,1,2,1,11,8,11,1,-1},
		{9,6,4,9,3,6,9,1,3,11,6,3,-1,-1,-1,-1},
		{8,11,1,8,1,0,11,6,1,9,1,4,6,4,1,-1},
		{3,11,6,3,6,0,0,6,4,-1,-1,-1,-1,-1,-1,-1},
		{6,4,8,11,6,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{7,10,6,7,8,10,8,9,10,-1,-1,-1,-1,-1,-1,-1},
		{0,7,3,0,10,7,0,9,10,6,7,10,-1,-1,-1,-1},
		{10,6,7,1,10,7,1,7,8,1,8,0,-1,-1,-1,-1},
		{10,6,7,10,7,1,1,7,3,-1,-1,-1,-1,-1,-1,-1},
		{1,2,6,1,6,8,1,8,9,8,6,7,-1,-1,-1,-1},
		{2,6,9,2,9,1,6,7,9,0,9,3,7,3,9,-1},
		{7,8,0,7,0,6,6,0,2,-1,-1,-1,-1,-1,-1,-1},
		{7,3,2,6,7,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{2,3,11,10,6,8,10,8,9,8,6,7,-1,-1,-1,-1},
		{2,0,7,2,7,11,0,9,7,6,7,10,9,10,7,-1},
		{1,8,0,1,7,8,1,10,7,6,7,10,2,3,11,-1},
		{11,2,1,11,1,7,10,6,1,6,7,1,-1,-1,-1,-1},
		{8,9,6,8,6,7,9,1,6,11,6,3,1,3,6,-1},
		{0,9,1,11,6,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{7,8,0,7,0,6,3,11,0,11,6,0,-1,-1,-1,-1},
		{7,11,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{3,0,8,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,1,9,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{8,1,9,8,3,1,11,7,6,-1,-1,-1,-1,-1,-1,-1},
		{10,1,2,6,11,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,2,10,3,0,8,6,11,7,-1,-1,-1,-1,-1,-1,-1},
		{2,9,0,2,10,9,6,11,7,-1,-1,-1,-1,-1,-1,-1},
		{6,11,7,2,10,3,10,8,3,10,9,8,-1,-1,-1,-1},
		{7,2,3,6,2,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{7,0,8,7,6,0,6,2,0,-1,-1,-1,-1,-1,-1,-1},
		{2,7,6,2,3,7,0,1,9,-1,-1,-1,-1,-1,-1,-1},
		{1,6,2,1,8,6,1,9,8,8,7,6,-1,-1,-1,-1},
		{10,7,6,10,1,7,1,3,7,-1,-1,-1,-1,-1,-1,-1},
		{10,7,6,1,7,10,1,8,7,1,0,8,-1,-1,-1,-1},
		{0,3,7,0,7,10,0,10,9,6,10,7,-1,-1,-1,-1},
		{7,6,10,7,10,8,8,10,9,-1,-1,-1,-1,-1,-1,-1},
		{6,8,4,11,8,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{3,6,11,3,0,6,0,4,6,-1,-1,-1,-1,-1,-1,-1},
		{8,6,11,8,4,6,9,0,1,-1,-1,-1,-1,-1,-1,-1},
		{9,4,6,9,6,3,9,3,1,11,3,6,-1,-1,-1,-1},
		{6,8,4,6,11,8,2,10,1,-1,-1,-1,-1,-1,-1,-1},
		{1,2,10,3,0,11,0,6,11,0,4,6,-1,-1,-1,-1},
		{4,11,8,4,6,11,0,2,9,2,10,9,-1,-1,-1,-1},
		{10,9,3,10,3,2,9,4,3,11,3,6,4,6,3,-1},
		{8,2,3,8,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1},
		{0,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,9,0,2,3,4,2,4,6,4,3,8,-1,-1,-1,-1},
		{1,9,4,1,4,2,2,4,6,-1,-1,-1,-1,-1,-1,-1},
		{8,1,3,8,6,1,8,4,6,6,10,1,-1,-1,-1,-1},
		{10,1,0,10,0,6,6,0,4,-1,-1,-1,-1,-1,-1,-1},
		{4,6,3,4,3,8,6,10,3,0,3,9,10,9,3,-1},
		{10,9,4,6,10,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{4,9,5,7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,8,3,4,9,5,11,7,6,-1,-1,-1,-1,-1,-1,-1},
		{5,0,1,5,4,0,7,6,11,-1,-1,-1,-1,-1,-1,-1},
		{11,7,6,8,3,4,3,5,4,3,1,5,-1,-1,-1,-1},
		{9,5,4,10,1,2,7,6,11,-1,-1,-1,-1,-1,-1,-1},
		{6,11,7,1,2,10,0,8,3,4,9,5,-1,-1,-1,-1},
		{7,6,11,5,4,10,4,2,10,4,0,2,-1,-1,-1,-1},
		{3,4,8,3,5,4,3,2,5,10,5,2,11,7,6,-1},
		{7,2,3,7,6,2,5,4,9,-1,-1,-1,-1,-1,-1,-1},
		{9,5,4,0,8,6,0,6,2,6,8,7,-1,-1,-1,-1},
		{3,6,2,3,7,6,1,5,0,5,4,0,-1,-1,-1,-1},
		{6,2,8,6,8,7,2,1,8,4,8,5,1,5,8,-1},
		{9,5,4,10,1,6,1,7,6,1,3,7,-1,-1,-1,-1},
		{1,6,10,1,7,6,1,0,7,8,7,0,9,5,4,-1},
		{4,0,10,4,10,5,0,3,10,6,10,7,3,7,10,-1},
		{7,6,10,7,10,8,5,4,10,4,8,10,-1,-1,-1,-1},
		{6,9,5,6,11,9,11,8,9,-1,-1,-1,-1,-1,-1,-1},
		{3,6,11,0,6,3,0,5,6,0,9,5,-1,-1,-1,-1},
		{0,11,8,0,5,11,0,1,5,5,6,11,-1,-1,-1,-1},
		{6,11,3,6,3,5,5,3,1,-1,-1,-1,-1,-1,-1,-1},
		{1,2,10,9,5,11,9,11,8,11,5,6,-1,-1,-1,-1},
		{0,11,3,0,6,11,0,9,6,5,6,9,1,2,10,-1},
		{11,8,5,11,5,6,8,0,5,10,5,2,0,2,5,-1},
		{6,11,3,6,3,5,2,10,3,10,5,3,-1,-1,-1,-1},
		{5,8,9,5,2,8,5,6,2,3,8,2,-1,-1,-1,-1},
		{9,5,6,9,6,0,0,6,2,-1,-1,-1,-1,-1,-1,-1},
		{1,5,8,1,8,0,5,6,8,3,8,2,6,2,8,-1},
		{1,5,6,2,1,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,3,6,1,6,10,3,8,6,5,6,9,8,9,6,-1},
		{10,1,0,10,0,6,9,5,0,5,6,0,-1,-1,-1,-1},
		{0,3,8,5,6,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{10,5,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{11,5,10,7,5,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{11,5,10,11,7,5,8,3,0,-1,-1,-1,-1,-1,-1,-1},
		{5,11,7,5,10,11,1,9,0,-1,-1,-1,-1,-1,-1,-1},
		{10,7,5,10,11,7,9,8,1,8,3,1,-1,-1,-1,-1},
		{11,1,2,11,7,1,7,5,1,-1,-1,-1,-1,-1,-1,-1},
		{0,8,3,1,2,7,1,7,5,7,2,11,-1,-1,-1,-1},
		{9,7,5,9,2,7,9,0,2,2,11,7,-1,-1,-1,-1},
		{7,5,2,7,2,11,5,9,2,3,2,8,9,8,2,-1},
		{2,5,10,2,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1},
		{8,2,0,8,5,2,8,7,5,10,2,5,-1,-1,-1,-1},
		{9,0,1,5,10,3,5,3,7,3,10,2,-1,-1,-1,-1},
		{9,8,2,9,2,1,8,7,2,10,2,5,7,5,2,-1},
		{1,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,8,7,0,7,1,1,7,5,-1,-1,-1,-1,-1,-1,-1},
		{9,0,3,9,3,5,5,3,7,-1,-1,-1,-1,-1,-1,-1},
		{9,8,7,5,9,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{5,8,4,5,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1},
		{5,0,4,5,11,0,5,10,11,11,3,0,-1,-1,-1,-1},
		{0,1,9,8,4,10,8,10,11,10,4,5,-1,-1,-1,-1},
		{10,11,4,10,4,5,11,3,4,9,4,1,3,1,4,-1},
		{2,5,1,2,8,5,2,11,8,4,5,8,-1,-1,-1,-1},
		{0,4,11,0,11,3,4,5,11,2,11,1,5,1,11,-1},
		{0,2,5,0,5,9,2,11,5,4,5,8,11,8,5,-1},
		{9,4,5,2,11,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{2,5,10,3,5,2,3,4,5,3,8,4,-1,-1,-1,-1},
		{5,10,2,5,2,4,4,2,0,-1,-1,-1,-1,-1,-1,-1},
		{3,10,2,3,5,10,3,8,5,4,5,8,0,1,9,-1},
		{5,10,2,5,2,4,1,9,2,9,4,2,-1,-1,-1,-1},
		{8,4,5,8,5,3,3,5,1,-1,-1,-1,-1,-1,-1,-1},
		{0,4,5,1,0,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{8,4,5,8,5,3,9,0,5,0,3,5,-1,-1,-1,-1},
		{9,4,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{4,11,7,4,9,11,9,10,11,-1,-1,-1,-1,-1,-1,-1},
		{0,8,3,4,9,7,9,11,7,9,10,11,-1,-1,-1,-1},
		{1,10,11,1,11,4,1,4,0,7,4,11,-1,-1,-1,-1},
		{3,1,4,3,4,8,1,10,4,7,4,11,10,11,4,-1},
		{4,11,7,9,11,4,9,2,11,9,1,2,-1,-1,-1,-1},
		{9,7,4,9,11,7,9,1,11,2,11,1,0,8,3,-1},
		{11,7,4,11,4,2,2,4,0,-1,-1,-1,-1,-1,-1,-1},
		{11,7,4,11,4,2,8,3,4,3,2,4,-1,-1,-1,-1},
		{2,9,10,2,7,9,2,3,7,7,4,9,-1,-1,-1,-1},
		{9,10,7,9,7,4,10,2,7,8,7,0,2,0,7,-1},
		{3,7,10,3,10,2,7,4,10,1,10,0,4,0,10,-1},
		{1,10,2,8,7,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{4,9,1,4,1,7,7,1,3,-1,-1,-1,-1,-1,-1,-1},
		{4,9,1,4,1,7,0,8,1,8,7,1,-1,-1,-1,-1},
		{4,0,3,7,4,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{4,8,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{9,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{3,0,9,3,9,11,11,9,10,-1,-1,-1,-1,-1,-1,-1},
		{0,1,10,0,10,8,8,10,11,-1,-1,-1,-1,-1,-1,-1},
		{3,1,10,11,3,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,2,11,1,11,9,9,11,8,-1,-1,-1,-1,-1,-1,-1},
		{3,0,9,3,9,11,1,2,9,2,11,9,-1,-1,-1,-1},
		{0,2,11,8,0,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{3,2,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{2,3,8,2,8,10,10,8,9,-1,-1,-1,-1,-1,-1,-1},
		{9,10,2,0,9,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{2,3,8,2,8,10,0,1,8,1,10,8,-1,-1,-1,-1},
		{1,10,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{1,3,8,9,1,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,9,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{0,3,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
		{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}
	};
}

FVoxelMeshData FVoxelMesher::MeshChunk(
	const FVoxelVolume& Volume,
	const FVoxelChunkCoord& Coord,
	const FSettings& Settings)
{
	const FVoxelSphereMapping& Map = Volume.GetMapping();
	const int32 LOD = FMath::Clamp(Settings.LOD, 0, FVoxelConstants::MaxLOD);
	const int32 Stride = 1 << LOD;
	const float BaseVoxel = Map.GetParams().VoxelSize;
	const float VoxelSize = BaseVoxel * static_cast<float>(Stride);
	constexpr int32 CS = FVoxelConstants::ChunkSize;

	const int32 Cells = FMath::Max(2, CS / Stride);
	// Pad 1 for seamless density; mesh all cells in the padded grid so borders meet
	constexpr int32 Pad = 1;
	const int32 Samples = Cells + 1 + 2 * Pad;
	const int32 SampleCount = Samples * Samples * Samples;

	TArray<float> Densities;
	TArray<int32> Materials;
	Densities.SetNumUninitialized(SampleCount);
	Materials.SetNumUninitialized(SampleCount);

	const int32 BaseMinX = Coord.X * CS - Pad * Stride;
	const int32 BaseMinY = Coord.Y * CS - Pad * Stride;
	const int32 BaseMinZ = Coord.Z * CS - Pad * Stride;

	for (int32 Z = 0; Z < Samples; ++Z)
	{
		for (int32 Y = 0; Y < Samples; ++Y)
		{
			for (int32 X = 0; X < Samples; ++X)
			{
				const FIntVector VC(
					BaseMinX + X * Stride,
					BaseMinY + Y * Stride,
					BaseMinZ + Z * Stride);
				// Corner sample: fast continuous density (neighbor-seamless). Dirty cells override.
				const FVector CornerWorld = Map.VoxelToWorldMin(VC);
				const FVoxelChunkCoord CC = FVoxelSphereMapping::VoxelToChunk(VC);
				const FVoxelChunk* Chunk = Volume.FindChunk(CC);
				float D = Map.SampleDensityFast(CornerWorld);
				int32 MatId = 1;
				if (Chunk && Chunk->bDirty)
				{
					const FVoxelLocalCoord LC = FVoxelSphereMapping::VoxelToLocal(VC);
					const FVoxelCell& Cell = Chunk->At(LC.X, LC.Y, LC.Z);
					D = Cell.Density;
					MatId = Cell.MaterialId != 0 ? Cell.MaterialId : 1;
				}
				else if (D > 0.0f)
				{
					// Full material path so ores / biomes / scars show in mesh colors
					MatId = Map.SampleMaterial(CornerWorld, D);
					if (MatId == 0) MatId = 1;
				}
				const int32 Idx = VoxelMC::GridIndex(X, Y, Z, Samples, Samples);
				Densities[Idx] = D;
				Materials[Idx] = MatId;
			}
		}
	}

	const FVector GridOrigin = Map.VoxelToWorldMin(FIntVector(BaseMinX, BaseMinY, BaseMinZ));

	return MeshDensityGrid(
		Densities, Materials,
		Samples, Samples, Samples,
		GridOrigin, VoxelSize,
		Volume.GetMaterials(), Settings,
		Pad);
}

FVoxelMeshData FVoxelMesher::MeshDensityGrid(
	const TArray<float>& Densities,
	const TArray<int32>& Materials,
	int32 SizeX, int32 SizeY, int32 SizeZ,
	const FVector& Origin,
	float VoxelSize,
	const FVoxelMaterialTable& MaterialsTable,
	const FSettings& Settings,
	int32 Pad)
{
	using namespace VoxelMC;

	FVoxelMeshData Mesh;
	if (SizeX < 2 || SizeY < 2 || SizeZ < 2)
	{
		return Mesh;
	}

	// Mesh only interior cells so neighbors share exact boundary faces without overlap shells.
	// Pad samples still feed corner densities on the boundary.
	const int32 SafePad = FMath::Clamp(Pad, 0, FMath::Min3(SizeX, SizeY, SizeZ) / 2 - 1);
	const int32 X0 = SafePad;
	const int32 Y0 = SafePad;
	const int32 Z0 = SafePad;
	const int32 X1 = SizeX - 1 - SafePad; // last cell origin index (inclusive loop uses <)
	const int32 Y1 = SizeY - 1 - SafePad;
	const int32 Z1 = SizeZ - 1 - SafePad;
	if (X1 <= X0 || Y1 <= Y0 || Z1 <= Z0)
	{
		return Mesh;
	}

	auto SampleD = [&](int32 X, int32 Y, int32 Z) -> float
	{
		return Densities[GridIndex(X, Y, Z, SizeX, SizeY)];
	};
	auto SampleM = [&](int32 X, int32 Y, int32 Z) -> int32
	{
		return Materials[GridIndex(X, Y, Z, SizeX, SizeY)];
	};

	// Shared edge vertex cache: key = edge id along grid
	// For simplicity use TMap of edge -> vertex index
	// Edge keys: encode (x,y,z,edgeDir) where edgeDir 0=X,1=Y,2=Z along that axis from (x,y,z)
	TMap<uint64, int32> EdgeVert;

	auto EdgeKey = [](int32 X, int32 Y, int32 Z, int32 Axis) -> uint64
	{
		// Pack into 64 bits
		return (uint64(uint32(X) & 0xFFFF) << 48)
			| (uint64(uint32(Y) & 0xFFFF) << 32)
			| (uint64(uint32(Z) & 0xFFFF) << 16)
			| uint64(Axis & 0xFF);
	};

	auto GetOrCreateEdgeVert = [&](int32 C0x, int32 C0y, int32 C0z, int32 C1x, int32 C1y, int32 C1z,
		float D0, float D1, int32 M0, int32 M1) -> int32
	{
		// Normalize edge orientation so shared edges match
		int32 Ax = C0x, Ay = C0y, Az = C0z, Bx = C1x, By = C1y, Bz = C1z;
		float Da = D0, Db = D1;
		int32 Ma = M0, Mb = M1;
		if (Ax > Bx || (Ax == Bx && Ay > By) || (Ax == Bx && Ay == By && Az > Bz))
		{
			Swap(Ax, Bx); Swap(Ay, By); Swap(Az, Bz);
			Swap(Da, Db); Swap(Ma, Mb);
		}
		int32 Axis = 0;
		if (Bx != Ax) Axis = 0;
		else if (By != Ay) Axis = 1;
		else Axis = 2;

		const uint64 Key = EdgeKey(Ax, Ay, Az, Axis);
		if (const int32* Found = EdgeVert.Find(Key))
		{
			return *Found;
		}

		const float T = FMath::Clamp((Settings.IsoLevel - Da) / (Db - Da + KINDA_SMALL_NUMBER), 0.f, 1.f);
		// Brace-init (not FVector PA(float,float,float)) — avoids most-vexing-parse as a function decl
		const FVector PA{ static_cast<float>(Ax), static_cast<float>(Ay), static_cast<float>(Az) };
		const FVector PB{ static_cast<float>(Bx), static_cast<float>(By), static_cast<float>(Bz) };
		const FVector Local = FMath::Lerp(PA, PB, T);
		const FVector World = Origin + Local * VoxelSize;

		const int32 SolidMat = (Da >= Settings.IsoLevel) ? Ma : Mb;
		const int32 MatId = SolidMat != 0 ? SolidMat : ((Ma != 0) ? Ma : (Mb != 0 ? Mb : 1));

		const int32 VI = Mesh.Positions.Num();
		Mesh.Positions.Add(World);
		Mesh.Normals.Add(FVector::UpVector);
		Mesh.UV0.Add(FVector2D(float(MatId), float(Settings.LOD)));
		FLinearColor Col = MaterialsTable.GetDebugColor(MatId);
		Col.R = FMath::Clamp(Col.R * 1.3f + 0.08f, 0.f, 1.f);
		Col.G = FMath::Clamp(Col.G * 1.3f + 0.08f, 0.f, 1.f);
		Col.B = FMath::Clamp(Col.B * 1.3f + 0.08f, 0.f, 1.f);
		Col.A = 1.f;
		Mesh.Colors.Add(Settings.bVertexColorsFromMaterial ? Col : FLinearColor::White);
		Mesh.Tangents.Add(FProcMeshTangent(FVector(1, 0, 0), false));
		Mesh.MaterialIds.Add(MatId);

		EdgeVert.Add(Key, VI);
		return VI;
	};

	// Corner grid indices for each cube
	const int32 COff[8][3] = {
		{0,0,0},{1,0,0},{1,1,0},{0,1,0},
		{0,0,1},{1,0,1},{1,1,1},{0,1,1}
	};

	for (int32 Z = Z0; Z < Z1; ++Z)
	{
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				float Cd[8];
				int32 Cm[8];
				int32 CubeIndex = 0;
				for (int32 C = 0; C < 8; ++C)
				{
					const int32 CX = X + COff[C][0];
					const int32 CY = Y + COff[C][1];
					const int32 CZ = Z + COff[C][2];
					Cd[C] = SampleD(CX, CY, CZ);
					Cm[C] = SampleM(CX, CY, CZ);
					// Solid (density >= iso) sets bit — classic MC uses inside as bit
					if (Cd[C] >= Settings.IsoLevel)
					{
						CubeIndex |= (1 << C);
					}
				}

				const int32 Edges = EdgeTable[CubeIndex];
				if (Edges == 0)
				{
					continue;
				}

				// Build 12 possible edge vertices for this cube
				int32 VertList[12];
				for (int32 E = 0; E < 12; ++E)
				{
					VertList[E] = -1;
					if (Edges & (1 << E))
					{
						const int32 A = EdgeCorner[E][0];
						const int32 B = EdgeCorner[E][1];
						const int32 Ax = X + COff[A][0], Ay = Y + COff[A][1], Az = Z + COff[A][2];
						const int32 Bx = X + COff[B][0], By = Y + COff[B][1], Bz = Z + COff[B][2];
						VertList[E] = GetOrCreateEdgeVert(Ax, Ay, Az, Bx, By, Bz, Cd[A], Cd[B], Cm[A], Cm[B]);
					}
				}

				// Emit triangles
				for (int32 i = 0; TriTable[CubeIndex][i] != -1; i += 3)
				{
					const int32 E0 = TriTable[CubeIndex][i];
					const int32 E1 = TriTable[CubeIndex][i + 1];
					const int32 E2 = TriTable[CubeIndex][i + 2];
					const int32 I0 = VertList[E0];
					const int32 I1 = VertList[E1];
					const int32 I2 = VertList[E2];
					if (I0 < 0 || I1 < 0 || I2 < 0)
					{
						continue;
					}

					// Wind outward from planet center (origin)
					const FVector& P0 = Mesh.Positions[I0];
					const FVector& P1 = Mesh.Positions[I1];
					const FVector& P2 = Mesh.Positions[I2];
					FVector FN = FVector::CrossProduct(P1 - P0, P2 - P0);
					const FVector Centroid = (P0 + P1 + P2) / 3.f;
					// Density solid inside → surface normal should point to air (outward ≈ radial for planet)
					if (FVector::DotProduct(FN, Centroid) < 0.f)
					{
						Mesh.Indices.Add(I0);
						Mesh.Indices.Add(I2);
						Mesh.Indices.Add(I1);
					}
					else
					{
						Mesh.Indices.Add(I0);
						Mesh.Indices.Add(I1);
						Mesh.Indices.Add(I2);
					}
				}
			}
		}
	}

	// Smooth normals
	if (Settings.bComputeNormals && Mesh.Positions.Num() > 0)
	{
		for (FVector& N : Mesh.Normals)
		{
			N = FVector::ZeroVector;
		}
		for (int32 I = 0; I + 2 < Mesh.Indices.Num(); I += 3)
		{
			const int32 IA = Mesh.Indices[I];
			const int32 IB = Mesh.Indices[I + 1];
			const int32 IC = Mesh.Indices[I + 2];
			if (!Mesh.Positions.IsValidIndex(IA) || !Mesh.Positions.IsValidIndex(IB) || !Mesh.Positions.IsValidIndex(IC))
			{
				continue;
			}
			const FVector FN = FVector::CrossProduct(
				Mesh.Positions[IB] - Mesh.Positions[IA],
				Mesh.Positions[IC] - Mesh.Positions[IA]);
			Mesh.Normals[IA] += FN;
			Mesh.Normals[IB] += FN;
			Mesh.Normals[IC] += FN;
		}
		for (int32 I = 0; I < Mesh.Normals.Num(); ++I)
		{
			if (!Mesh.Normals[I].Normalize())
			{
				const FVector R = Mesh.Positions[I].GetSafeNormal();
				Mesh.Normals[I] = R.IsNearlyZero() ? FVector::UpVector : R;
			}
			if (FVector::DotProduct(Mesh.Normals[I], Mesh.Positions[I]) < 0.f)
			{
				Mesh.Normals[I] = -Mesh.Normals[I];
			}
			FVector T = FVector::CrossProduct(Mesh.Normals[I], FVector(0, 0, 1));
			if (T.SizeSquared() < 1e-6f)
			{
				T = FVector::CrossProduct(Mesh.Normals[I], FVector(0, 1, 0));
			}
			T.Normalize();
			if (Mesh.Tangents.IsValidIndex(I))
			{
				Mesh.Tangents[I] = FProcMeshTangent(T, false);
			}
		}
	}

	return Mesh;
}
