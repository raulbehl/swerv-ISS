#include <iostream>
#include <fstream>
#include <sstream>

bool compareRD (uint32_t, uint32_t, uint32_t);
bool compareRS1 (uint32_t, uint32_t, uint32_t);
bool compareRS2 (uint32_t, uint32_t, uint32_t);

extern "C"
int issInit (int, char*);
extern "C"
void issExec ();
extern "C"
void issDecode ();
extern "C"
int isRTypeInst ();
extern "C"
int isITypeInst ();
extern "C"
int isBTypeInst ();
extern "C"
int isUTypeInst ();
extern "C"
int isJTypeInst ();
extern "C"
int issCompareInst (uint32_t, uint32_t);
extern "C"
int issCompareR (uint32_t, uint32_t,
                 uint32_t, uint32_t,
                 uint32_t, uint32_t);
extern "C"
int issCompareI (uint32_t, uint32_t,
                 uint32_t, uint32_t);
extern "C"
int issCompareSB (uint32_t, uint32_t,
                  uint32_t, uint32_t);
extern "C"
int issCompareUJ (uint32_t, uint32_t);

extern "C"
void issSetPC    (uint32_t);

extern "C"
void issSetIntReg   (uint32_t, uint32_t);

extern "C"
void issSetCstReg   (uint32_t, uint32_t);
