/*
 * This program simulates cache behavior with various configurations (LRU for milestone 2)
 * CSF Assignment 3: Cache Simulator
 * Jonathan Wang
 * jwang612@jh.edu
 */

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <sstream>

using std::cin;
using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;

// struct to represent a cache block
struct Block {
  bool valid;
  bool dirty;
  uint32_t tag;
  uint32_t loadTime;
  
  // setting default values
  Block() : valid(false), dirty(false), tag(0), loadTime(0) {}
};

// struct to represent a cache set
struct Set {
  vector<Block> blocks;
  
  Set(int numBlocks) : blocks(numBlocks) {}
};

// struct to hold cache configuration
struct CacheConfig {
  int numSets;
  int numBlocks;
  int blockSize;
  bool writeAllocate;
  bool writeThrough;
  bool useLru;  // true for LRU, false for FIFO
  
  // calculated values
  int offsetBits;
  int indexBits;
  int tagBits;
};

// struct to hold resulting simulation statistics
struct Stats {
  int totalLoads;
  int totalStores;
  int loadHits;
  int loadMisses;
  int storeHits;
  int storeMisses;
  int totalCycles;
  
  Stats() : totalLoads(0), totalStores(0), loadHits(0), 
            loadMisses(0), storeHits(0), storeMisses(0), totalCycles(0) {}
};

// helper function declarations
bool parseArguments(int argc, char **argv, CacheConfig &config);
bool isPowerOfTwo(int n);
void simulateCache(const CacheConfig &config, Stats &stats);
void extractAddressParts(uint32_t address, const CacheConfig &config,
                         uint32_t &tag, uint32_t &index);
int findBlock(const Set &set, uint32_t tag);
int findEvictionBlock(const Set &set, bool useLru);
void handleLoad(vector<Set> &cache, uint32_t address, 
                const CacheConfig &config, Stats &stats, uint32_t &globalTime);
void handleStore(vector<Set> &cache, uint32_t address,
                 const CacheConfig &config, Stats &stats, uint32_t &globalTime);

int main(int argc, char **argv) {
  CacheConfig config;
  
  // parse command line arguments & check for invalid parameters
  if (!parseArguments(argc, argv, config)) {
    return 1;
  }
  
  // run simulation
  Stats stats;
  simulateCache(config, stats);
  
  // lastly, print results
  cout << "Total loads: " << stats.totalLoads << endl;
  cout << "Total stores: " << stats.totalStores << endl;
  cout << "Load hits: " << stats.loadHits << endl;
  cout << "Load misses: " << stats.loadMisses << endl;
  cout << "Store hits: " << stats.storeHits << endl;
  cout << "Store misses: " << stats.storeMisses << endl;
  cout << "Total cycles: " << stats.totalCycles << endl;
  
  return 0;
}

// parse and validate command line arguments
bool parseArguments(int argc, char **argv, CacheConfig &config) {
  if (argc != 7) {
    cerr << "Error: Expected 6 arguments" << endl;
    cerr << "Usage key: ./csim <sets> <blocks> <bytes> <write-allocate|no-write-allocate> "
         << "<write-through|write-back> <lru|fifo>" << endl;
    return false;
  }
  
  // parse numeric parameters (args 1-3)
  config.numSets = std::stoi(argv[1]);
  config.numBlocks = std::stoi(argv[2]);
  config.blockSize = std::stoi(argv[3]);
  
  // validate numeric parameters
  if (!isPowerOfTwo(config.numSets) || config.numSets <= 0) {
    cerr << "Error: Number of sets must be a positive power of 2" << endl;
    return false;
  }
  
  if (!isPowerOfTwo(config.numBlocks) || config.numBlocks <= 0) {
    cerr << "Error: Number of blocks must be a positive power of 2" << endl;
    return false;
  }
  
  if (!isPowerOfTwo(config.blockSize) || config.blockSize < 4) {
    cerr << "Error: Block size must be a power of 2 and at least 4" << endl;
    return false;
  }
  
  // parse write-allocate parameter (arg 4)
  string writeAllocStr = argv[4];
  if (writeAllocStr == "write-allocate") {
    config.writeAllocate = true;
  } else if (writeAllocStr == "no-write-allocate") {
    config.writeAllocate = false;
  } else {
    cerr << "Error: Write allocate must be 'write-allocate' or 'no-write-allocate'" << endl;
    return false;
  }
  
  // parse write-through parameter (arg 5)
  string writeThroughStr = argv[5];
  if (writeThroughStr == "write-through") {
    config.writeThrough = true;
  } else if (writeThroughStr == "write-back") {
    config.writeThrough = false;
  } else {
    cerr << "Error: Write policy must be 'write-through' or 'write-back'" << endl;
    return false;
  }
  
  // chaching strategy (arg 6)
  string evictionStr = argv[6];
  if (evictionStr == "lru") {
    config.useLru = true;
  } else if (evictionStr == "fifo") {
    config.useLru = false;
  } else {
    cerr << "Error: Eviction policy must be 'lru' or 'fifo'" << endl;
    return false;
  }
  
  // check for invalid combinations
  if (!config.writeAllocate && !config.writeThrough) {
    cerr << "Error: no-write-allocate cannot be combined with write-back" << endl;
    return false;
  }
  
  // lastly, bit positions
  config.offsetBits = (int)log2(config.blockSize);
  config.indexBits = (int)log2(config.numSets);
  config.tagBits = 32 - config.offsetBits - config.indexBits;
  
  return true;
}

// check if a number is a power of 2 and is positive
bool isPowerOfTwo(int n) {
  return n > 0 && (n & (n - 1)) == 0; 
}

// get tag and index from address
void extractAddressParts(uint32_t address, const CacheConfig &config,
                         uint32_t &tag, uint32_t &index) {
  // remove offset bits
  uint32_t addrWithoutOffset = address >> config.offsetBits;
  
  // extract index
  uint32_t indexMask = (1U << config.indexBits) - 1;
  index = addrWithoutOffset & indexMask;
  
  // lastly, extract tag
  tag = addrWithoutOffset >> config.indexBits;
}

// Find valid block with matching tag in a set (-1 if not found)
int findBlock(const Set &set, uint32_t tag) {
  for (size_t i = 0; i < set.blocks.size(); i++) {
    if (set.blocks[i].valid && set.blocks[i].tag == tag) {
      return i;
    }
  }
  return -1;
}

// find block to evict using LRU or FIFO
int findEvictionBlock(const Set &set, bool useLru) {
  // first check for invalid blocks
  for (size_t i = 0; i < set.blocks.size(); i++) {
    if (!set.blocks[i].valid) {
      return i;
    }
  }
  
  // if all blocks valid, find victim block
  int victimIndex = 0;
  uint32_t oldestTime = set.blocks[0].loadTime;
  
  for (size_t i = 1; i < set.blocks.size(); i++) {
    if (set.blocks[i].loadTime < oldestTime) {
      oldestTime = set.blocks[i].loadTime;
      victimIndex = i;
    }
  }
  
  useLru = useLru; // adding this line here to stop the warning message
  // for MS2 only implementing LRU, so didn't need to use this parameter
  return victimIndex;
}

// handle a (l)oad operation
void handleLoad(vector<Set> &cache, uint32_t address, 
                const CacheConfig &config, Stats &stats, uint32_t &globalTime) {
  stats.totalLoads++;
  
  uint32_t tag, index;
  extractAddressParts(address, config, tag, index);
  
  Set &set = cache[index];
  int blockIndex = findBlock(set, tag);
  
  if (blockIndex != -1) {
    // then it's a hit
    stats.loadHits++;
    stats.totalCycles += 1;
    
    // for LRU, update access time for the block
    if (config.useLru) {
      set.blocks[blockIndex].loadTime = globalTime++;
    }
  } else {
    // it's a miss
    stats.loadMisses++;
    
    // load from memory (costs 100 cycles per 4-byte block)
    int blocksToLoad = config.blockSize / 4;
    stats.totalCycles += 1 + 100 * blocksToLoad;
    
    // find block to replace
    int victimIndex = findEvictionBlock(set, config.useLru);
    
    // if evicting dirty block in write-back, write to memory
    if (set.blocks[victimIndex].valid && set.blocks[victimIndex].dirty) {
      stats.totalCycles += 100 * blocksToLoad;
    }
    
    // lastly, load the new block into cache
    set.blocks[victimIndex].valid = true;
    set.blocks[victimIndex].tag = tag;
    set.blocks[victimIndex].dirty = false;
    set.blocks[victimIndex].loadTime = globalTime++;
  }
}

// handle a (s)tore operation
void handleStore(vector<Set> &cache, uint32_t address,
                 const CacheConfig &config, Stats &stats, uint32_t &globalTime) {
  stats.totalStores++;
  
  uint32_t tag, index;
  extractAddressParts(address, config, tag, index);
  
  Set &set = cache[index];
  int blockIndex = findBlock(set, tag);
  
  if (blockIndex != -1) {
    // then it's a hit
    stats.storeHits++;
    stats.totalCycles += 1;
    
    // for LRU, update access time
    if (config.useLru) {
      set.blocks[blockIndex].loadTime = globalTime++;
    }
    
    // handle the write policy
    if (config.writeThrough) {
      // if write-through, write to memory
      stats.totalCycles += 100;
    } else {
      // if write-back, mark as dirty
      set.blocks[blockIndex].dirty = true;
    }
  } else {
    // it's a miss
    stats.storeMisses++;
    
    if (config.writeAllocate) {
      // load block into cache
      int blocksToLoad = config.blockSize / 4;
      stats.totalCycles += 1 + 100 * blocksToLoad;
      
      // find block to replace
      int victimIndex = findEvictionBlock(set, config.useLru);
      
      // if evicting dirty block in write-back, write to memory
      if (set.blocks[victimIndex].valid && set.blocks[victimIndex].dirty) {
        stats.totalCycles += 100 * blocksToLoad;
      }
      
      // load the new block into cache
      set.blocks[victimIndex].valid = true;
      set.blocks[victimIndex].tag = tag;
      set.blocks[victimIndex].loadTime = globalTime++;
      
      // handle write policy
      if (config.writeThrough) {
        // if write-through, write to memory
        set.blocks[victimIndex].dirty = false;
        stats.totalCycles += 100;
      } else {
        // if write-back, mark as dirty
        set.blocks[victimIndex].dirty = true;
      }
    } else {
      // if no-write-allocate to begin with, just write to memory
      stats.totalCycles += 1 + 100;
    }
  }
}

// main cache simulation function
void simulateCache(const CacheConfig &config, Stats &stats) {
  // initiailize cache and global time tracker
  vector<Set> cache;
  for (int i = 0; i < config.numSets; i++) {
    cache.push_back(Set(config.numBlocks));
  }
  uint32_t globalTime = 0;
  
  // read and process the trace file
  string line;
  while (std::getline(cin, line)) {
    if (line.empty()) {
      continue;
    }
    
    std::istringstream iss(line);
    char operation;     // (s)tore or (l)oad
    string addressStr;  // memory address
    int ignored;        // ignoring the third field for this assignement
    
    iss >> operation >> addressStr >> ignored;
    
    // convert address from hex string to uint32_t
    uint32_t address = std::stoul(addressStr, nullptr, 16);
    
    if (operation == 'l') {
      handleLoad(cache, address, config, stats, globalTime);
    } else if (operation == 's') {
      handleStore(cache, address, config, stats, globalTime);
    }
  }
}