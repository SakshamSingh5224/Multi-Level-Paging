#include "pagetable.h"
#include "cache.h"

#include <unordered_map>
#include <map>
#include <math.h>
#include <fstream>
#include <unistd.h>

int main(int argc, char **argv)
{
    PageTable *pageTable;      // instantiate page table
    OutputOptionsType *output; // instantiate output object

    std::map<uint32_t, uint32_t> TLB; // cache table of <VPN, PFN>
    std::map<uint32_t, uint32_t> LRU; // least recent accessed table of <VPN, Access Time>

    int pageSize;               // instantiate page size
    int addressProcessingLimit; // instantiate address limit
    int cacheCapacity;          // instantiate size of TLB
    char *outputType;           // instantiate type of output
    FILE *tracefile;            // instantiate tracefile

    pageTable = new PageTable();               // initialize page table
    output = new OutputOptionsType();          // initialize output object
    pageTable->offsetSize = DEFAULTOFFSET;     // initialize offset size
    addressProcessingLimit = DEFAULTADDRLIMIT; // initialize address limit
    cacheCapacity = DEFAULTCACHESIZE;          // initialize size of TLB
    outputType = DEFAULTOUTPUTTYPE;            // initialize output type

    // check optional arguments
    int opt;
    while ((opt = getopt(argc, argv, "n:c:o:")) != -1)
    {
        switch (opt)
        {
        case 'n':
            // sets number of addresses needed to go through
            addressProcessingLimit = atoi(optarg);
            break;
        case 'c':
            // gets cache capacity of adresses
            cacheCapacity = atoi(optarg);
            break;
        case 'o':
            // gets type of output
            outputType = optarg;
            break;
        default:
            exit(EXIT_FAILURE);
        }
    }

    // require at least 2 arguments
    if (argc - optind < 2)
    {
        fprintf(stderr, "%s: too few arguments\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // verify working tracefile
    tracefile = fopen(argv[optind++], "rb");
    if (tracefile == NULL)
    {
        fprintf(stderr, "cannot open %s for reading: \n", argv[optind++]);
        exit(1);
    }

    pageSize = pow(2, pageTable->offsetSize);                // set page size
    pageTable->numLevels = argc - optind;                    // get number of levels in page table
    pageTable->bitsPerLevel = new int[pageTable->numLevels]; // create array to store bit count per level
    pageTable->bitShift = new int[pageTable->numLevels];     // create array to store bit shift per level
    pageTable->entriesPerLevel = new int[pageTable->numLevels];

    // loop through each page level
    for (int i = optind; i < argc; i++)
    {
        // counting bits at each level
        pageTable->bitsPerLevel[i - optind] = atoi(argv[i]);                                  // store bits count for level i
        pageTable->entriesPerLevel[i - optind] = pow(2, pageTable->bitsPerLevel[i - optind]); // store entries for level i

        // finding bitshift for each level
        pageTable->totalPageBits += atoi(argv[i]);                                  // total page bits at level i
        pageTable->offsetSize -= (atoi(argv[i]));                                   // offset at level i
        pageTable->bitShift[i - optind] = (DEFAULTSIZE - pageTable->totalPageBits); // amount of bit shift at level i
    }

    // create vpn, offset, masks
    pageTable->vpnMask = ((1 << pageTable->totalPageBits) - 1) << pageTable->offsetSize; // vpn mask
    pageTable->offsetMask = (1 << pageTable->offsetSize) - 1;                            // offset mask
    pageTable->pageLookupMask = new uint32_t[pageTable->numLevels];                      // array of page lookup masks
    pageTable->pageLookup = new uint32_t[pageTable->numLevels];                          // array of page lookup masks

    // find page lookup masks at each level
    for (int i = 0; i < pageTable->numLevels; i++)
    {
        pageTable->pageLookupMask[i] = ((1 << pageTable->bitsPerLevel[i]) - 1) << (pageTable->bitShift[i]);
    }

    pageTable->addressCount = 0;
    uint32_t newFrame = 0;

    // remain within address to process limits
    while (!feof(tracefile) && pageTable->addressCount != addressProcessingLimit)
    {
        // next address
        p2AddrTr *address_trace = new p2AddrTr();

        // if another address exists
        if (NextAddress(tracefile, address_trace))
        {
            pageTable->addressCount++; // keeping track of 
            pageTable->vpn = virtualAddressToPageNum(address_trace->addr, pageTable->vpnMask, pageTable->offsetSize); // find address VPN
            pageTable->offset = virtualAddressToPageNum(address_trace->addr, pageTable->offsetMask, 0); // find address offset

            // page lookups per level
            for (int i = 0; i < pageTable->numLevels; i++)
            {
                pageTable->pageLookup[i] = virtualAddressToPageNum(address_trace->addr, pageTable->pageLookupMask[i], pageTable->bitShift[i]);
            }

            // search TLB for VPN
            uint32_t PFN;

            if (TLB.find(pageTable->vpn) != TLB.end())
            {
                // TLB hit
                std::cout << "tlb hit" << std::endl;

                // PFN from TLB
                PFN = TLB[pageTable->vpn];

                // update LRU with most recent addressCount
                LRU[pageTable->vpn] = pageTable->addressCount;
            }
            else
            {
                // TLB miss, walk PageTable
                
                // search PageTable for VPN
                Map *found = pageLookup(pageTable, pageTable->vpn);

                // found VPN in PageTable
                if (found != NULL)
                {
                    // TLB miss, PageTable hit
                    std::cout << "tlb miss, pagetable hit" << std::endl;

                    // check if cache is full
                    if (TLB.size() == cacheCapacity)
                    {
                        // find oldest VPN in LRU
                        uint32_t oldestKey;
                        uint32_t oldestValue;
                        oldestValue = pageTable->addressCount;
                        for (std::map<uint32_t, uint32_t>::iterator iter = LRU.begin(); iter != LRU.end(); ++iter)
                        {
                            if (oldestValue > iter->second)
                            {
                                oldestKey = iter->first;
                                oldestValue = iter->second;
                            }
                        }

                        // erase oldest from TLB and LRU
                        TLB.erase(oldestKey);
                        LRU.erase(oldestKey);
                    }

                    // insert into TLB and LRU
                    TLB[found->vpn] = found->frame;
                    LRU[found->vpn] = pageTable->addressCount;

                    PFN = found->frame;
                }
                else
                {
                    // TLB miss, PageTable miss
                    std::cout << "tlb miss, pagetable miss" << std::endl;

                    // insert vpn and new frame into page table
                    pageInsert(pageTable, pageTable->vpn, newFrame);

                    // check if cache is full
                    if (TLB.size() == cacheCapacity)
                    {
                        // find oldest VPN in LRU
                        uint32_t oldestKey;
                        uint32_t oldestValue;
                        oldestValue = pageTable->addressCount;
                        for (std::map<uint32_t, uint32_t>::iterator iter = LRU.begin(); iter != LRU.end(); ++iter)
                        {
                            if (oldestValue > iter->second)
                            {
                                oldestKey = iter->first;
                                oldestValue = iter->second;
                            }
                        }

                        // erase oldest from TLB and LRU
                        TLB.erase(oldestKey);
                        LRU.erase(oldestKey);
                    }

                    // insert into TLB and LRU
                    TLB[pageTable->vpn] = newFrame;
                    LRU[pageTable->vpn] = pageTable->addressCount;

                    PFN = newFrame;

                    newFrame++;
                }
            }

            std::cout << "PFN: " << std::hex << PFN << std::endl;
        }
    }

    // report_summary(pagetable->pageSize, 0, 0, pagetable->instructionsProcessed, 0, 0); // creates summary, need to update 0's to actual arguments
    return 0;
};
