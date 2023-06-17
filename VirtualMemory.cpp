#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include <algorithm>


#define ROOT 0
#define NOT_IN_MEMORY 0
#define NOT_VISITED -1

void VMinitialize()
{
    for (uint64_t i = 0; i < PAGE_SIZE; i++)
    {
        PMwrite(ROOT + i, NOT_IN_MEMORY);
    }
}

int inFramesVisited(int* framesVisited, int num)
{
    for (int i = 0; i < TABLES_DEPTH+1; i++)
    {
        if (framesVisited[i] == NOT_VISITED)
        {
            return false;
        }
        if (framesVisited[i] == num)
        {
            return true;
        }
    }
    return false;
}

uint64_t readBits(uint64_t virtualAddress, uint64_t bitsRead, uint64_t fixedVirtualAddress)
{

    // Calculate how many bits are not processed yet
    uint64_t rawBits = fixedVirtualAddress - bitsRead - 1;

    uint64_t returnVal = 0;
    for (int i = 0; i < OFFSET_WIDTH; i++)
    {
        // Make an int with ones only on the part we want to read
        returnVal += 1LL << (rawBits - i);
    }

    // Reading the number from virtual address, for example:
    // 00..1111..00 & 00..1010..01 == 00..1010..00
    returnVal = virtualAddress & returnVal;

    // Translating the bits to a value between 2^0 and 2^OFFSET_WIDTH
    return returnVal >> (fixedVirtualAddress - OFFSET_WIDTH - bitsRead);
}

void cyclicalTraverse(int frameNum, int depth, long long& cyclicalDistance,
                     long long pageSwappedIn, long long pageNum,
                     long long &cyclicalPage, long long& childIndex,
                     long long& frameIndex)
{
    pageNum = pageNum << ((long long) OFFSET_WIDTH);
    int checkValue = 0;
    int curCyclicalDistance;
    int childPageNum;
    for (long long i = 0; i < PAGE_SIZE; i++)
    {
        PMread((frameNum * PAGE_SIZE) + i, &checkValue);
        if (checkValue != 0)
        {
            if (depth == TABLES_DEPTH - 1) // In a leaf's father
            {
                childPageNum = pageNum + i;
                curCyclicalDistance = std::min(NUM_PAGES - std::abs(pageSwappedIn-childPageNum),
                                               std::abs(pageSwappedIn-childPageNum));

                if (curCyclicalDistance > cyclicalDistance)
                {
                    cyclicalDistance = curCyclicalDistance;
                    cyclicalPage = childPageNum;
                    childIndex = i;
                    frameIndex = frameNum;
                }
            }
            else
            {
                cyclicalTraverse(checkValue, depth + 1,
                                 cyclicalDistance, pageSwappedIn,
                                 pageNum + i,
                                 cyclicalPage, childIndex, frameIndex);
            }

        }
    }
}

int treeTraverse(int frameNum, int depth, int& maxFrame, int* frameVisited, int prevFrame, int prevFrameIndex)
{
    if (frameNum > maxFrame)
    {
        maxFrame = frameNum;
    }

    if (depth == TABLES_DEPTH) // In a leaf
    {
        return 0;
    }

    int counter = 0;
    int checkValue = 0;
    for (int i = 0; i < PAGE_SIZE; i++)
    {
        PMread((frameNum * PAGE_SIZE) + i, &checkValue);
        if (checkValue != 0)
        {
            // Finding node without children
            checkValue = treeTraverse(checkValue, depth+1, maxFrame, frameVisited, frameNum, i);
            if (checkValue != 0 && !inFramesVisited(frameVisited, checkValue))
            {
                return checkValue;
            }
        }
        else
        {
            counter++;
        }
    }

    if (counter == PAGE_SIZE && !inFramesVisited(frameVisited, frameNum))
    {
        if (prevFrame != -1)
        {
            PMwrite((prevFrame * PAGE_SIZE) + prevFrameIndex, 0);
        }
        return frameNum;
    }
    return 0;
}

int makeRoom(int* frameVisited, long long pageSwappedIn)
{
    int maxFrame = 0;
    int f = treeTraverse(0, 0, maxFrame, frameVisited, -1, 0);
    if (f != 0)
    {
        return f;
    }
    if (maxFrame + 1 < NUM_FRAMES)
    {
        return maxFrame + 1;
    }

    long long cyclicalDistance = -1;
    long long cyclicalPage = 0;
    long long childIndex = 0;
    long long frameIndex = 0;

    cyclicalTraverse(0, 0, cyclicalDistance, pageSwappedIn, 0, cyclicalPage, childIndex, frameIndex);
    int frameIndexChild;
    PMread((frameIndex * PAGE_SIZE) + childIndex, &frameIndexChild);
    PMevict(frameIndexChild, cyclicalPage);
    PMwrite((frameIndex * PAGE_SIZE) + childIndex, 0);
    return frameIndexChild;
}

void initializeFramesVisited(int* frameVisited)
{
    frameVisited[0] = ROOT;
    for (int i = 1; i < TABLES_DEPTH; i++)
    {
        frameVisited[i] = NOT_VISITED;
    }
}


uint64_t VMhandler(uint64_t virtualAddress, int fixedVirtualAddress)
{

    // Calculate how many bits are not processed yet
    word_t addr1;
    uint64_t currentRow = 0;
    int frameVisited[TABLES_DEPTH+1];
    long long pageNumber = 0;
    initializeFramesVisited(frameVisited);
    long long pageSwappedIn = virtualAddress >> OFFSET_WIDTH;
    int k = 0;

    // Reading from tree
    for (int i = 0; i < fixedVirtualAddress - OFFSET_WIDTH; i += OFFSET_WIDTH)
    {
        pageNumber = pageNumber << ((long long) OFFSET_WIDTH);

        pageNumber += (long long)readBits(virtualAddress, i, fixedVirtualAddress) ;
        PMread(currentRow + readBits(virtualAddress, i, fixedVirtualAddress), &addr1);
        if (addr1 == NOT_IN_MEMORY)
        {
            // Finding virtual page num
            addr1 = makeRoom(frameVisited, pageSwappedIn);
            // Writing to papa his son/daughter
            PMwrite(currentRow + readBits(virtualAddress, i, fixedVirtualAddress), addr1);
            if (i + OFFSET_WIDTH >= fixedVirtualAddress - OFFSET_WIDTH)
            {
                PMrestore(addr1, pageNumber);
            }
            else
            {
                // Making Rows in Frame 0
                for (int j = 0; j < PAGE_SIZE; j++)
                {
                    PMwrite(addr1 * PAGE_SIZE + j, 0);
                }
            }
        }
        // Current Frame!
        currentRow = addr1 * PAGE_SIZE;
        frameVisited[++k] = addr1;
    }

    return currentRow;
}


int VMread(uint64_t virtualAddress, word_t* value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE || value == nullptr)
    {
        return 0;
    }
    int fixedVirtualAddress = VIRTUAL_ADDRESS_WIDTH + (VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH);
    uint64_t currentRow = VMhandler(virtualAddress, fixedVirtualAddress);

    // Reading from root
    PMread(currentRow + readBits(virtualAddress, fixedVirtualAddress - OFFSET_WIDTH, fixedVirtualAddress), value);
    return 1;
}


int VMwrite(uint64_t virtualAddress, word_t value)
{
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE)
    {
        return 0;
    }
    int fixedVirtualAddress = VIRTUAL_ADDRESS_WIDTH + (VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH);
    uint64_t currentRow = VMhandler(virtualAddress, fixedVirtualAddress);

    // Reading from root
    PMwrite(currentRow + readBits(virtualAddress, fixedVirtualAddress - OFFSET_WIDTH, fixedVirtualAddress), value);
    return 1;
}