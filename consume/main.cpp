#include <atomic>
#include <future>
#include <chrono>
#include <iostream>
#include <cassert>

using namespace std;

__attribute__((aligned(256))) atomic<int*> Guard(nullptr);
__attribute__((aligned(256))) int Payload = 0;

void delayedWrite()
{
    this_thread::sleep_for(chrono::milliseconds(1));
    Payload = 42;
    Guard.store(&Payload, memory_order_release);
}

struct Status
{
    int* g;
    int p;
};

__attribute__((noinline)) void readBatch(Status* status, int batchSize)
{
    int* g = 0;
    int p = 0;
    for (int i = 0; i < batchSize; i++)
    {
        __asm volatile("":::"memory");
        
        g = Guard.load(memory_order_consume);
        if (g != nullptr)
            p = *g;
        
        status[i].g = g;
        status[i].p = p;
    }
}

static const int BATCH_SIZE = 1000;
Status StatusBlock[BATCH_SIZE];

int main(int argc, char * argv[])
{
    float totalTrialTime = 0;
    int numTrials = 0;
    
    bool hasReorderingBug = false;
    while (!hasReorderingBug)
    {
        Guard.store(0, memory_order_relaxed);
        Payload = 0;
        
        auto handle = async(launch::async, delayedWrite);
        
        bool writeHappened = false;
        while (!writeHappened)
        {
            auto start_time = chrono::high_resolution_clock::now();
            readBatch(StatusBlock, BATCH_SIZE);
            auto end_time = chrono::high_resolution_clock::now();
            
            totalTrialTime += chrono::duration_cast<chrono::nanoseconds>(end_time - start_time).count();
            numTrials += BATCH_SIZE;
            
            if (StatusBlock[BATCH_SIZE - 1].g != 0)
            {
                writeHappened = true;
                for (int i = 0; i < BATCH_SIZE; i++)
                {
                    if (StatusBlock[i].g != nullptr && StatusBlock[i].p != 42)
                        hasReorderingBug = true;
                }
            }
            
            if (totalTrialTime > 1000000000.f)
            {
                cout << "Trial time = " << totalTrialTime / numTrials << " ns" << endl;
                totalTrialTime = 0;
                numTrials = 0;
            }
            
            if (hasReorderingBug)
            {
                for (int i = 0; i < BATCH_SIZE; i++)
                {
                    cout << "g = " << StatusBlock[i].g << ", p = ";
                    if (StatusBlock[i].g != 0)
                        cout << StatusBlock[i].p << endl;
                    else
                        cout << "<not loaded>" << endl;
                }
            }
        }
    }
    return 0;
}
