#include "serial_client_queue.h"

TRPCQueueMessage::TRPCQueueMessage(PPort Port, PRPCRequest Request): Port(Port), Request(Request)
{
    Cancelled.store(false);
    Done.store(false);
}