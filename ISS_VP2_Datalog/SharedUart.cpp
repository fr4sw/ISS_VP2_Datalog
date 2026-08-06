// ============================================================================
// Fichier   : SharedUart.cpp
// ============================================================================
#include "SharedUart.h"

static SharedUartOwner currentOwner = SHARED_UART_NONE;

bool sharedUartAcquire(SharedUartOwner owner)
{
    bool alreadyOwnedBySameUser = (currentOwner == owner);
    bool free = (currentOwner == SHARED_UART_NONE);

    if ((free == false) && (alreadyOwnedBySameUser == false))
    {
        return false;
    }

    currentOwner = owner;
    return true;
}

void sharedUartRelease(SharedUartOwner owner)
{
    if (currentOwner == owner)
    {
        currentOwner = SHARED_UART_NONE;
    }
}

SharedUartOwner sharedUartCurrentOwner()
{
    return currentOwner;
}
