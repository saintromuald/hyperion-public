// port_record.h -- --record: a game played for so many frames, then its replay kept
// (port_record.cpp).
#ifndef PORT_RECORD_H
#define PORT_RECORD_H

// --record (port_record.cpp): after each frame of a game, whether it is time to leave it;
// after the game, its replay copied where asked and the program ended.
void PortRecordFrame(void);
void PortRecordAfterGame(void);

#endif
