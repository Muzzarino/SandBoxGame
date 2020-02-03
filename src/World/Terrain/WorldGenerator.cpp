/*
 * WorldGenerator.cpp
 *
 * Created by tursh on 9/28/19.
*/

#include <Utils/Log.h>
#include <World/Terrain/Block.h>
#include <World/Terrain/Chunk.h>
#include "World/Terrain/WorldGenerator.h"
#include <World/World.h>

const unsigned int MAX_WORLD_GENERATION_HEIGHT = 256;

WorldGenerator::WorldGenerator(World *world, ChunkManager &chunkManager)
        : world_(world), chunkManager_(chunkManager) {}

static void fillChunk(Block *blocks, Block block)
{
    for (int i = 0; i < CUBED_CHUNK_SIZE; ++i)
        blocks[i] = block;
}

static unsigned char generateState(double *cornerGroundLevels, int &cornerDownCount, int &underBlockCorner, int &y)
{
    unsigned char state = 0;
    int midYCornersCount = 0;

    for (int corner = 0; corner < 4; ++corner)
    {
        double relativeLevel = cornerGroundLevels[corner] - y;

        if (relativeLevel < Blocks::CUBE_SIZE / 2.0f)
        {
            ++cornerDownCount;
            if (relativeLevel < -Blocks::CUBE_SIZE / 2.0f)
                underBlockCorner = corner;

            //Set the corner flag
            state += (unsigned char) 1 << corner;
        }

        if (Blocks::CUBE_SIZE / .25f < relativeLevel &&
            relativeLevel < Blocks::CUBE_SIZE * .75f)
            ++midYCornersCount;
    }

    //Set the midY flag
    if (midYCornersCount > 1)
        state += (unsigned char) 0B00100000;

    return state;
}

void WorldGenerator::run()
{
    while (running_)
    {
        glm::ivec3 chunkPosition = chunkManager_.getChunkToLoad();

        //If the chunk manager returns this position, it means, there no chunk to load
        if (chunkPosition == glm::ivec3(INT_MIN))
        {
            wait();
            continue;
        }

        //Create the block 3D matrix and fill it with air
        Block *blocks = new Block[CUBED_CHUNK_SIZE];
        fillChunk(blocks, Blocks::AIR_BLOC);

        //If the chunk is under the world generation range, it is going to be only stone blocs
        if (chunkPosition.y < 0)
        {
            //Set all stone blocks
            fillChunk(blocks, {2, 0});
        }
            //If the chunk is higher than the world generation range, it is going to be only air blocs
        else if (chunkPosition.y > MAX_WORLD_GENERATION_HEIGHT / CHUNK_SIZE); //Noting to do, chunk is already air blocs
        else
        {
            //Get all ground level in chunk and chunk border blocs
            double groundLevel[19 * 19];
            double higher = 0, lower = 256;

            {
                int xEndPosition = (chunkPosition.x + 1) * CHUNK_SIZE + 2,
                        zEndPosition = (chunkPosition.z + 1) * CHUNK_SIZE + 2,
                        i = 0;

                for (int x = chunkPosition.x * CHUNK_SIZE - 1; x < xEndPosition; ++x)
                    for (int z = chunkPosition.z * CHUNK_SIZE - 1; z < zEndPosition; ++z)
                    {
                        groundLevel[i] = pn.noise(x / (double) (CHUNK_SIZE * 4), z / (double) (CHUNK_SIZE * 4), 0) *
                                         CHUNK_SIZE * 6;
                        if (higher < groundLevel[i])
                            higher = groundLevel[i];
                        if (lower > groundLevel[i])
                            lower = groundLevel[i];
                        ++i;
                    }
            }

            if (higher / CHUNK_SIZE < chunkPosition.y)
                goto end; //If there is no ground high enough to be in the chunk, then the chunk is empty

            if (lower / CHUNK_SIZE - 1 > chunkPosition.y)
            {
                fillChunk(blocks, {2, 0});
                goto end; // If there is no ground lower enough to have
            }

            //Make ground level relative to the chunk
            for (unsigned int i = 0; i < 19 * 19; ++i)
                groundLevel[i] -= chunkPosition.y * CHUNK_SIZE;

            double averageGroundLevels[17 * 17];

            for (int xa = 0; xa < 17; ++xa)
                for (int za = 0; za < 17; ++za)
                {
                    double &averageGroundLevel = averageGroundLevels[xa * 17 + za] = 0;
                    for (int xb = 0; xb < 3; ++xb)
                        for (int zb = 0; zb < 3; ++zb)
                        {
                            averageGroundLevel += groundLevel[(xa + xb) * 19 + (za + zb)];
                        }

                    averageGroundLevel /= 9;
                }

            for (int x = 0; x < CHUNK_SIZE; ++x)
                for (int z = 0; z < CHUNK_SIZE; ++z)
                {
                    double cornerGroundLevels[4] =
                            {
                                    averageGroundLevels[x * 17 + z],
                                    averageGroundLevels[x * 17 + z + 1],
                                    averageGroundLevels[(x + 1) * 17 + z],
                                    averageGroundLevels[(x + 1) * 17 + z + 1]
                            };

                    double
                            blockGroundLevel = 0;

                    for (int corner = 0; corner < 4; ++corner)
                        blockGroundLevel += cornerGroundLevels[corner];

                    blockGroundLevel /= 4;

                    //If the ground level is not in the chunk range, then it wont affect it
                    if (blockGroundLevel < 0 || blockGroundLevel >= CHUNK_SIZE)
                        continue;

                    for (int y = std::min<int>(blockGroundLevel, CHUNK_SIZE - 1); y >= 0; --y)
                    {
                        if (y >= blockGroundLevel - 2)
                        {
                            int cornerDownCount = 0, underBlockCorner = -1;
                            unsigned char state = generateState(cornerGroundLevels, cornerDownCount, underBlockCorner,
                                                                y);


                            //If the block under in another chunk has more than 1 corner down, no block.
                            if (y == 0 && state && underBlockCorner)
                            {
                                glm::ivec3 underChunkPosition = chunkPosition;
                                --underChunkPosition.y;

                                Chunk *underChunk = world_->getChunkByChunkPosition(
                                        underChunkPosition);
                                if (underChunk != nullptr)
                                {
                                    unsigned char underChunkBlockState = underChunk->getBlock({x, 15, z}).state;

                                    if (underChunkBlockState)
                                    {
                                        int underChunkBlockCornerDownCount = 0;

                                        for (int corner = 0; corner < 4; ++corner)
                                            underChunkBlockCornerDownCount += (underChunkBlockState >> corner) & 1;

                                        if (underChunkBlockCornerDownCount > 1)
                                            state = 0B00001111;
                                    }
                                }
                            }

                            //If the top block is stated and this block has more than 1 corner down, remove it
                            if (cornerDownCount > 1 && y < CHUNK_SIZE - 1)
                                blocks[x + CHUNK_SIZE * ((y + 1) + CHUNK_SIZE * z)] = Blocks::AIR_BLOC;

                            //If the top block on the other chunk is stated while this one has more than 1 corner down, remove it.
                            if (y == 15 && cornerDownCount > 1)
                            {
                                Chunk *topChunk = world_->getChunkByChunkPosition(
                                        chunkPosition + glm::ivec3(0, 1, 0));
                                if (topChunk != nullptr && topChunk->getBlock({x, 0, z}).state)
                                {
                                    topChunk->setBlock({x, 0, z}, Blocks::AIR_BLOC);
                                }
                            }

                            //
                            if (underBlockCorner != -1)
                            {
                                if (!(state >> (underBlockCorner ^ 1) & 1))
                                    state += (unsigned char) 1 << (underBlockCorner ^ 1);

                                if (!(state >> (underBlockCorner ^ 2) & 1))
                                    state += (unsigned char) 1 << (underBlockCorner ^ 2);
                            }

                            //Set the block
                            blocks[x + CHUNK_SIZE * (y + CHUNK_SIZE * z)] = {1, state};

                        }
                        else
                        {
                            //Set grass block or stone block
                            blocks[x + CHUNK_SIZE * (y + CHUNK_SIZE * z)] =
                                    {(short) (y + chunkPosition.y * CHUNK_SIZE < higher - 3 ? 2 : 1), 0};
                        }

                    }
                }
        }

        end:

        auto *newChunk = new Chunk(blocks, world_, chunkPosition, false);
        world_->addChunk(newChunk);
        newChunk->updateChunksAround();

    }

}
