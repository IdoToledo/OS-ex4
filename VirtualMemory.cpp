#include "VirtualMemory.h"
#include "PhysicalMemory.h"
#include "MemoryConstants.h"

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


//    return (virtualAddress & ((1LL << (rawBits+1))-1)) >> (VIRTUAL_ADDRESS_WIDTH - OFFSET_WIDTH - bitsRead);
}

int treeTraverse(int frameNum, int depth, int& maxFrame,
                 long long& cyclicalDistance,
                 long long pageSwappedIn,
                 long long pageNum,
                 int* frameVisited)
{
    pageNum = pageNum << ((long long) OFFSET_WIDTH);
    if (frameNum > maxFrame)
    {
        maxFrame = frameNum;
    }
    if (depth == TABLES_DEPTH)
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
            checkValue = treeTraverse(checkValue, depth+1, maxFrame, frameVisited);
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
    return (counter == PAGE_SIZE && !inFramesVisited(frameVisited, checkValue)) ? (frameNum) : (0);
}

int makeRoom(int pageNum, int* frameVisited)
{
    int maxFrame = 0;
    int f = treeTraverse(0, 0, maxFrame, frameVisited);
    if (f != 0)
    {
        return f;
    }
    if (maxFrame + 1 < NUM_FRAMES)
    {
        return maxFrame + 1;
    }
}

void initializeFramesVisited(int* frameVisited)
{
    frameVisited[0] = ROOT;
    for (int i = 1; i < TABLES_DEPTH; i++)
    {
        frameVisited[i] = NOT_VISITED;
    }
}

int VMread(uint64_t virtualAddress, word_t* value)
{
    uint64_t currentFrame = helper(virtualAddress);
    int fixedVirtualAddress = VIRTUAL_ADDRESS_WIDTH + (VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH);

    // Reading from root
    PMread(currentFrame + readBits(virtualAddress, fixedVirtualAddress - OFFSET_WIDTH, fixedVirtualAddress), value);
    return 1;
}


int VMwrite(uint64_t virtualAddress, word_t value)
{
    // Don't need to check virtualAddress < RAM_SIZE

    // Calculate how many bits are not processed yet
    word_t addr1;
    word_t addr2;
    uint64_t currentFrame = 0;
    int frameVisited[TABLES_DEPTH+1];
    initializeFramesVisited(frameVisited);
    int fixedVirtualAddress = VIRTUAL_ADDRESS_WIDTH + (VIRTUAL_ADDRESS_WIDTH % OFFSET_WIDTH);
    int j = 0;
    // Reading from tree
    for (int i = 0; i < fixedVirtualAddress - OFFSET_WIDTH; i += OFFSET_WIDTH)
    {
        PMread(currentFrame + readBits(virtualAddress, i, fixedVirtualAddress), &addr1);
        if (addr1 == NOT_IN_MEMORY)
        {
            // Finding virtual page num
            addr1 = makeRoom(ROOT, frameVisited);
            // Writing to papa his son/daughter
            PMwrite(currentFrame + readBits(virtualAddress, i, fixedVirtualAddress), addr1);
        }
        // Current Frame!
        currentFrame = addr1 * PAGE_SIZE;
        // Making Rows in Frame 0
        for (int j = 0; j < PAGE_SIZE; j++)
        {
            PMwrite(currentFrame + j, 0);
        }
        frameVisited[++j] = addr1;
    }

    // Reading from root
    PMwrite(currentFrame + readBits(virtualAddress, fixedVirtualAddress - OFFSET_WIDTH, fixedVirtualAddress), value);
    return 1;
}