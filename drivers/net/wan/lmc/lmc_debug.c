#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>

#include "lmc_debug.h"

#ifdef DEBUG
#ifdef LMC_PACKET_LOG
void lmcConsoleLog(char *type, unsigned char *ucData, int iLen)
{
  int iNewLine = 1;
  char str[80], *pstr;
  
  sprintf(str, KERN_DEBUG "lmc: %s: ", type);
  pstr = str+strlen(str);
  
  if(iLen > 240){
      printk(KERN_DEBUG "lmc: Printing 240 chars... out of: %d\n", iLen);
    iLen = 240;
  }
  else{
      printk(KERN_DEBUG "lmc: Printing %d chars\n", iLen);
  }

  while(iLen > 0) 
    {
      sprintf(pstr, "%02x ", *ucData);
      pstr+=3;
      ucData++;
      if( !(iNewLine % 20))
	{
	  sprintf(pstr, "\n");
	  printk(str);
	  sprintf(str, KERN_DEBUG "lmc: %s: ", type);
	  pstr=str+strlen(str);
	}
      iNewLine++;
      iLen--;
    }
  sprintf(pstr, "\n");
  printk(str);
}
#endif
#endif

#ifdef DEBUG
u32 lmcEventLogIndex;
u32 lmcEventLogBuf[LMC_EVENTLOGSIZE * LMC_EVENTLOGARGS];

void lmcEventLog(u32 EventNum, u32 arg2, u32 arg3)
{
  lmcEventLogBuf[lmcEventLogIndex++] = EventNum;
  lmcEventLogBuf[lmcEventLogIndex++] = arg2;
  lmcEventLogBuf[lmcEventLogIndex++] = arg3;
  lmcEventLogBuf[lmcEventLogIndex++] = jiffies;

  lmcEventLogIndex &= (LMC_EVENTLOGSIZE * LMC_EVENTLOGARGS) - 1;
}
#endif  

void lmc_trace(struct net_device *dev, char *msg){
#ifdef LMC_TRACE
    unsigned long j = jiffies + 3; 

    if(in_interrupt()){
        printk("%s: * %s\n", dev->name, msg);
    }
    else {
        printk("%s: %s\n", dev->name, msg);
        while(time_before(jiffies, j))
            schedule();
    }
#endif
}


