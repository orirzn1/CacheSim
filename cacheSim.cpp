#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <memory>
#include <vector>

using std::FILE;
using std::string;
using std::cout;
using std::endl;
using std::cerr;
using std::ifstream;
using std::stringstream;

enum class Operation
{
    READ = 0,
    WRITE = 1
};

class cacheBlock
{
private:
    int tag;
    int accessNumber;
    bool dirtyBit;
    int offset;
    uint32_t address;
    bool validBit;
    
public:
    explicit cacheBlock(int tag, int accessNumber, int offset, uint32_t address, bool validBit) : tag(tag), accessNumber(accessNumber), offset(offset), dirtyBit(false), address(address), validBit(validBit)
    {}
    
    int getTag()
    {
        return tag;
    }
    
    void updateAccessNumber(int update)
    {
        accessNumber = update;
    }
    
    int getAccessNumber()
    {
        return accessNumber;
    }
    
    bool isDirty()
    {
        return dirtyBit;
    }
    
    void setToDirty()
    {
        dirtyBit = true;
    }
    
    uint32_t getAddress()
    {
        return address;
    }
    
    bool isValid()
    {
        return validBit;
    }
    
    void invalidate()
    {
        validBit = false;
    }
};

class simpleCache
{
private:
    std::vector<std::unique_ptr<cacheBlock> > memDataVector;
    int numOfBlocks;
    int blockSize;

public:
    explicit simpleCache(int numOfBlocks, int blockSize) : numOfBlocks(numOfBlocks), blockSize(blockSize)
    {
        memDataVector.reserve(numOfBlocks);
        for (int i = 0; i < numOfBlocks; i++)
        {
            memDataVector.push_back(std::unique_ptr<cacheBlock>(new cacheBlock(0, 0, 0, 0, false)));
        }
    }
    
    bool thereIsSpace(int tag, int set)
    {
        if(!memDataVector[set].get()->isValid())
            return true;
        
        return false;
    }
    
    bool accessBlock(int tag, int set, int accessNumber, Operation op) //return true if hit, false if miss
    {
        if(!memDataVector[set].get()->isValid())
            return false;
        
        if(memDataVector[set].get()->getTag() == tag)
        {
            memDataVector[set].get()->updateAccessNumber(accessNumber);
            if(op == Operation::WRITE)
                memDataVector[set].get()->setToDirty();
            return true;
        }
        
        return false;
    }
    
    void addBlock(int tag, int set, int accessNumber, int offset, uint32_t address, Operation op)
    {
        memDataVector[set] = std::unique_ptr<cacheBlock>(new cacheBlock(tag, accessNumber, offset, address, true));
        if(op == Operation::WRITE) //might need to change it, with this implementation both layers are dirty when we add a new write
            memDataVector[set].get()->setToDirty();
    }
    
    void removeBlock(int set)
    {
        memDataVector[set].get()->invalidate();
    }
    
    int getBlockAccessNum(int set)
    {
        return memDataVector[set].get()->getAccessNumber();
    }
    
    uint32_t getBlockAddress(int set)
    {
        return memDataVector[set].get()->getAddress();
    }
    
    bool getDirtyStatus(int set)
    {
        return memDataVector[set].get()->isDirty();
    }
    
    int getBlockTag(int set)
    {
        return memDataVector[set].get()->getTag();
    }
    
};

class cacheLayer
{
private:
    std::vector<std::unique_ptr<simpleCache>> layerVector;
    int accessCount;
    int missCount;
    int numSubLayers;
    int numBlocks;
    int blockSize;
    
    int offsetBits;
    int setBits;
    
    bool isFullAssoc;
    
public:
    explicit cacheLayer(int associativity, int numBlocks, int blockSize) : missCount(0), accessCount(0), numBlocks(numBlocks), blockSize(blockSize)
    {
        numSubLayers = associativity;

        layerVector.reserve(numSubLayers);
        
        for(int i = 0; i < numSubLayers; i++)
        {
            layerVector.push_back(std::unique_ptr<simpleCache>(new simpleCache(numBlocks/associativity, blockSize)));
        }
        
        offsetBits = log(blockSize) / log(2);
        
        setBits = log(numBlocks/associativity) / log(2);
        
        if(setBits == 0)
            isFullAssoc = true;
        else
            isFullAssoc = false;
    }
    
    int calcOffset(uint32_t address)
    {
        uint32_t bitmask = (1 << offsetBits) - 1; //create a mask of 1s of appropriate size
        return (int)(address & bitmask); //perform bitwise and
    }
    
    int calcTag(uint32_t address)
    {
        int totalOffset = offsetBits + setBits;
        address >>= totalOffset;
        return (int)address;
    }
    
    int calcSet(uint32_t address)
    {
        address >>= offsetBits;
        uint32_t bitmask = (1 << setBits) - 1; //create a mask of 1s of appropriate size
        return (int)(address & bitmask); //perform bitwise and
    }
    
    void replaceLRU(int tag, int set, int numOp, int offset, uint32_t address, int* removedBlockAddress, bool* removedBlockisDirty, Operation op)
    {
        int indexOfLRU = 0;
        int minAccessCount = layerVector[0].get()->getBlockAccessNum(set);
        for(int i = 0; i < numSubLayers; i++)
        {
            if(layerVector[i].get()->getBlockAccessNum(set) < minAccessCount)
            {
                minAccessCount = layerVector[i].get()->getBlockAccessNum(set);
                indexOfLRU = i;
            }
        }
        
        int addy = layerVector[indexOfLRU].get()->getBlockAddress(set);
        *removedBlockAddress = addy;
        *removedBlockisDirty = layerVector[indexOfLRU].get()->getDirtyStatus(set);
        layerVector[indexOfLRU].get()->addBlock(tag, set, numOp, offset, address, op);
    }
    
    void addBlock(uint32_t address, int numOp, int* removedBlockAddress, bool* removedBlockisDirty, Operation op)
    {
        int tag = calcTag(address);
        int set = calcSet(address);
        int offset = calcOffset(address);
        
        for(auto& x : layerVector)
        {
            if(x.get()->thereIsSpace(tag, set))
            {
                x.get()->addBlock(tag, set, numOp, offset, address, op);
                return;
            }
        }
        
        replaceLRU(tag, set, numOp, offset, address, removedBlockAddress, removedBlockisDirty, op);
    }
    
    void removeBlock(uint32_t address)
    {
        int set = calcSet(address);
        int tag = calcTag(address);
        
        for(auto& x : layerVector)
        {
            if(x->getBlockTag(set) == tag)
            {
                x->removeBlock(set);
            }
        }
    }
    
    bool execute(uint32_t address, int numOp, Operation op)
    {
        accessCount++;
        int tag = calcTag(address);
        int set = calcSet(address);
        
        for(auto& x : layerVector)
        {
            if(x.get()->accessBlock(tag, set, numOp, op))
                return true;
        }
        missCount++;
        return false;
    }
    
    void updateAccessNumber(uint32_t address, int numOp, Operation op)
    {
        int tag = calcTag(address);
        int set = calcSet(address);
        for(auto& x : layerVector)
        {
            x.get()->accessBlock(tag, set, numOp, op);
        }
    }
    
    double calcMissRate()
    {
        return std::round(1000.0*missCount / accessCount) / 1000.0;
    }
};

class cacheController
{
private:
    int m_cacheAccessCount;
    int m_totalTime;
    cacheLayer L1;
    cacheLayer L2;
    int blockSize;
    int MemCyc;
    int L1Cyc;
    int L2Cyc;
    bool wrAllocPolicy;

public:
    explicit cacheController(int BSize, int L1size, int L2size, int L1assoc, int L2assoc, int MemCyc, int L1Cyc, int L2Cyc, bool wrAllocPolicy) : blockSize(std::pow(2,BSize)), L1(std::pow(2, L1assoc), std::pow(2,L1size)/std::pow(2,BSize), std::pow(2,BSize)), L2(std::pow(2, L2assoc), std::pow(2,L2size)/std::pow(2,BSize), std::pow(2,BSize)), MemCyc(MemCyc), L1Cyc(L1Cyc), L2Cyc(L2Cyc), wrAllocPolicy(wrAllocPolicy)
    {

    }
    
    double getAverageTime()
    {
        return std::round(1000.0*m_totalTime / m_cacheAccessCount) / 1000.0;
    }
    
    double getL1MissRate()
    {
        return L1.calcMissRate(); 
    }
    
    double getL2MissRate()
    {
        return L2.calcMissRate();
    }
    
    void execute(Operation op, uint32_t address)
    {
        //Add functionality: if replacing LRU in L1 and the block is dirty then need to write to L2, and update LRU in L2 accordingly? // double check this
        int defaultAddy = -1;
        int* removedBlockAddress = &defaultAddy;
        m_cacheAccessCount++;
        if(L1.execute(address, m_cacheAccessCount, op)) //if hit in L1
        {
            m_totalTime += L1Cyc;
            //L2.updateAccessNumber(address, m_cacheAccessCount, op);
            return;
        }
        else if(L2.execute(address, m_cacheAccessCount, op)) //miss in L1 and hit in L2
        {
            m_totalTime = m_totalTime + L1Cyc + L2Cyc;
            bool boolean = false; //This bool will check if removed block was dirty
            L1.addBlock(address, m_cacheAccessCount, removedBlockAddress, &boolean, op);
            if(boolean)
                L2.updateAccessNumber(*removedBlockAddress, m_cacheAccessCount, op);
            return;
        }
        else //cache miss
        {
            m_totalTime = m_totalTime + L1Cyc + L2Cyc + MemCyc;
            if(op == Operation::WRITE && !wrAllocPolicy)
            {
                //no change to cache after miss, we write directly to memory
                return;
            }
            bool boolean = false; //This bool will check if removed block was dirty
            L1.addBlock(address, m_cacheAccessCount, removedBlockAddress, &boolean, op);
            if(boolean)
                L2.updateAccessNumber(*removedBlockAddress, m_cacheAccessCount, op);
            *removedBlockAddress = -1;
            L2.addBlock(address, m_cacheAccessCount, removedBlockAddress, &boolean, op);
            if(*removedBlockAddress != -1)
            {
                L1.removeBlock(*removedBlockAddress);
            }
            //if L2 replaces an existing block then make sure the existing block is removed from L1 as well --> Figure out way to know which block you removed from L2 --> different layers will have different tag/set for the same address
            //update
        }
    }
    
};

int main(int argc, char **argv)
{

	if (argc < 19) {
		cerr << "Not enough arguments" << endl;
		return 0;
	}

	// Get input arguments

	// File
	// Assuming it is the first argument
	char* fileString = argv[1];
	ifstream file(fileString); //input file stream
	string line;
	if (!file || !file.good()) {
		// File doesn't exist or some other error
		cerr << "File not found" << endl;
		return 0;
	}

	unsigned MemCyc = 0, BSize = 0, L1Size = 0, L2Size = 0, L1Assoc = 0,
			L2Assoc = 0, L1Cyc = 0, L2Cyc = 0, WrAlloc = 0;

	for (int i = 2; i < 19; i += 2) {
		string s(argv[i]);
		if (s == "--mem-cyc") {
			MemCyc = atoi(argv[i + 1]);
		} else if (s == "--bsize") {
			BSize = atoi(argv[i + 1]);
		} else if (s == "--l1-size") {
			L1Size = atoi(argv[i + 1]);
		} else if (s == "--l2-size") {
			L2Size = atoi(argv[i + 1]);
		} else if (s == "--l1-cyc") {
			L1Cyc = atoi(argv[i + 1]);
		} else if (s == "--l2-cyc") {
			L2Cyc = atoi(argv[i + 1]);
		} else if (s == "--l1-assoc") {
			L1Assoc = atoi(argv[i + 1]);
		} else if (s == "--l2-assoc") {
			L2Assoc = atoi(argv[i + 1]);
		} else if (s == "--wr-alloc") {
			WrAlloc = atoi(argv[i + 1]);
		} else {
			cerr << "Error in arguments" << endl;
			return 0;
		}
	}
    
    cacheController cache(BSize, L1Size, L2Size, L1Assoc, L2Assoc, MemCyc, L1Cyc, L2Cyc, WrAlloc);

	while (getline(file, line)) {

		stringstream ss(line);
		string address;
		char operation = 0; // read (R) or write (W)
		if (!(ss >> operation >> address)) {
			// Operation appears in an Invalid format
			cout << "Command Format error" << endl;
			return 0;
		}
		string cutAddress = address.substr(2); // Removing the "0x" part of the address

        if(operation == 'r')
        {
            cache.execute(Operation::READ, (uint32_t)std::strtoul(cutAddress.c_str(), nullptr, 16));
        }
        else if(operation == 'w')
        {
            cache.execute(Operation::WRITE, (uint32_t)std::strtoul(cutAddress.c_str(), nullptr, 16));
        }
	}

    std::cout << std::fixed << std::setprecision(3) << "L1miss=" << cache.getL1MissRate() << " L2miss=" << cache.getL2MissRate() << " AccTimeAvg=" << cache.getAverageTime() << std::endl;


	return 0;
}

//--mem-cyc 100 --bsize 3 --wr-alloc 1 --l1-size 4 --l1-assoc 1 --l1-cyc 1 --l2-size 6 --l2-assoc 0 --l2-cyc 5
//unsigned BSize = 3, L1Size = 4, L2Size = 6, L1Assoc = 1, L2Assoc = 0, MemCyc = 100, L1Cyc = 1, L2Cyc = 5, WrAlloc = 1;

//./cacheSim example3_trace --mem-cyc 10 --bsize 2 --wr-alloc 1 --l1-size 4 --l1-assoc 1 --l1-cyc 1 --l2-size 4 --l2-assoc 2 --l2-cyc 5
//unsigned BSize = 2, L1Size = 4, L2Size = 4, L1Assoc = 1, L2Assoc = 2, MemCyc = 10, L1Cyc = 1, L2Cyc = 5, WrAlloc = 1;
//./cacheSim example2_trace --mem-cyc 50 --bsize 4 --wr-alloc 1 --l1-size 6 --l1-assoc 1 --l1-cyc 2 --l2-size 8 --l2-assoc 2 --l2-cyc 4
//unsigned BSize = 4, L1Size = 6, L2Size = 8, L1Assoc = 1, L2Assoc = 2, MemCyc = 50, L1Cyc = 2, L2Cyc = 4, WrAlloc = 1;

/*cache.execute(Operation::READ, 0x10000000);
cache.execute(Operation::READ, 0x10000010);
cache.execute(Operation::READ, 0x10000020);
cache.execute(Operation::READ, 0x10000030);
cache.execute(Operation::READ, 0x10000040);
cache.execute(Operation::READ, 0x10000050);
cache.execute(Operation::READ, 0x10000060);
cache.execute(Operation::READ, 0x10000070);
cache.execute(Operation::READ, 0x10000080);
cache.execute(Operation::READ, 0x10000090);
cache.execute(Operation::READ, 0x100000A0);
cache.execute(Operation::READ, 0x100000B0);
cache.execute(Operation::READ, 0x100000C0);
cache.execute(Operation::READ, 0x100000E0);
cache.execute(Operation::READ, 0x100000F0);
cache.execute(Operation::WRITE, 0x10000008);
cache.execute(Operation::WRITE, 0x1000001C);
cache.execute(Operation::WRITE, 0x10000028);
cache.execute(Operation::WRITE, 0x1000003C);
cache.execute(Operation::WRITE, 0x10000048);
cache.execute(Operation::WRITE, 0x1000005C);
cache.execute(Operation::WRITE, 0x10000068);
cache.execute(Operation::WRITE, 0x1000007C);
cache.execute(Operation::WRITE, 0x10000088);
cache.execute(Operation::WRITE, 0x1000009C);
cache.execute(Operation::WRITE, 0x100000A8);
cache.execute(Operation::WRITE, 0x100000BC);
cache.execute(Operation::WRITE, 0x100000C8);
cache.execute(Operation::WRITE, 0x100000EC);
cache.execute(Operation::WRITE, 0x100000F8);*/

/*
 r 0x00000000
 w 0x00000004
 r 0x00100000
 w 0x00000000
 r 0x0000000C
 r 0x00000010
 r 0x0000001C
 r 0x00000020
 r 0x0000002C
 r 0x00000030
 r 0x0000003C
 r 0x00000040
 r 0x00000008
 w 0x00000044 */

/*cache.execute(Operation::READ, 0x00000000);
cache.execute(Operation::WRITE, 0x00000004);
cache.execute(Operation::READ, 0x00100000);
cache.execute(Operation::WRITE, 0x00000000);
cache.execute(Operation::READ, 0x0000000C);
cache.execute(Operation::READ, 0x00000010);
cache.execute(Operation::READ, 0x0000001C);
cache.execute(Operation::READ, 0x00000020);
cache.execute(Operation::READ, 0x0000002C);
cache.execute(Operation::READ, 0x00000030);
cache.execute(Operation::READ, 0x0000003C);
cache.execute(Operation::READ, 0x00000040);
cache.execute(Operation::READ, 0x00000008);
cache.execute(Operation::WRITE, 0x00000044);*/

/*
w 0x00000000
r 0x00010000
r 0x00020000
r 0x00030000
r 0x00040000
r 0x00000000
*/
/*cache.execute(Operation::WRITE, 0x00000000);
cache.execute(Operation::READ, 0x00010000);
cache.execute(Operation::READ, 0x00020000);
cache.execute(Operation::READ, 0x00030000);
cache.execute(Operation::READ, 0x00040000);
cache.execute(Operation::READ, 0x00000000); // should be a hit in L2*/
