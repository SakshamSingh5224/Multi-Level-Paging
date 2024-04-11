#include "level.h"

#define DEFAULT_MAP_VALUE -1

/**
 * @brief Recursively inserts a VPN->PFN mapping in the page table.
 *
 * @param level The level to insert into.
 * @param virtualAddress The virtual address to insert.
 * @param newFrame The new frame to assign the inserted virtual address.
 */
void pageInsert(Level *level, uint32_t virtualAddress, uint32_t newFrame) {
    if (level == nullptr || level->pageTable == nullptr) {
        // Handle invalid level pointer or page table pointer
        return;
    }

    int currentDepth = level->depth;
    uint32_t indexToInsert = level->pageTable->pageLookup[currentDepth];
    uint32_t vpn = virtualAddressToPageNum(virtualAddress, level->pageTable->vpnMask, level->pageTable->offsetSize);

    // Leaf node
    if (currentDepth == level->pageTable->numLevels - 1) {
        if (level->mappings == nullptr) {
            // Initialize mappings array if not already initialized
            level->mappings = new Map[level->pageTable->entriesPerLevel[currentDepth]];
            if (level->mappings == nullptr) {
                // Handle memory allocation failure
                return;
            }
            // Initialize mappings array to default values
            for (int i = 0; i < level->pageTable->entriesPerLevel[currentDepth]; ++i) {
                level->mappings[i] = {DEFAULT_MAP_VALUE, DEFAULT_MAP_VALUE};
            }
        }

        // Insert VPN->PFN mapping at the specified index
        level->mappings[indexToInsert] = {vpn, newFrame};
    } else { // Non-leaf node
        if (level->nextLevel[indexToInsert] == nullptr) {
            // Create new level if entry doesn't exist at index
            level->nextLevel[indexToInsert] = new Level();
            if (level->nextLevel[indexToInsert] == nullptr) {
                // Handle memory allocation failure
                return;
            }
            level->nextLevel[indexToInsert]->pageTable = level->pageTable;
            level->nextLevel[indexToInsert]->depth = currentDepth + 1;

            // Initialize next level array to nullptr
            int size = level->pageTable->entriesPerLevel[level->nextLevel[indexToInsert]->depth];
            level->nextLevel[indexToInsert]->nextLevel = new Level*[size]();
            if (level->nextLevel[indexToInsert]->nextLevel == nullptr) {
                // Handle memory allocation failure
                delete level->nextLevel[indexToInsert];
                level->nextLevel[indexToInsert] = nullptr;
                return;
            }
        }

        // Recursively insert into the next level
        pageInsert(level->nextLevel[indexToInsert], virtualAddress, newFrame);
    }
}

