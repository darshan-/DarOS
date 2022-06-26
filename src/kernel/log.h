#pragma once

void log(char* s);
void logf(char* fmt, ...);
void* forEachLog(void (*f)(char*));
void* forEachNewLog(void* last, void (*f)(char*));
