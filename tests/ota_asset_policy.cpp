#include "ota_asset_policy.h"
#include <cassert>
int main(){
 assert(matchesPublishedBoardAsset("elma-esp32s3-board25-v0.1.59-test.1.bin","esp32s3",25,"v0.1.59-test.1"));
 assert(!matchesPublishedBoardAsset("elma-esp32s3-board14-v0.1.59-test.1.bin","esp32s3",25,"v0.1.59-test.1"));
 assert(!matchesPublishedBoardAsset("elma-esp32-board25-v0.1.59-test.1.bin","esp32s3",25,"v0.1.59-test.1"));
 assert(!matchesPublishedBoardAsset("firmware.bin","esp32s3",25,"v0.1.59-test.1"));
 assert(!matchesPublishedBoardAsset("elma-esp32s3-board25-v0.1.58.bin","esp32s3",25,"v0.1.59-test.1"));
 assert(!matchesPublishedBoardAsset("elma-esp32s3-board0-v1.bin","esp32s3",0,"v1"));
 assert(comparePublishedVersions("v0.1.58","v0.1.59-test.1")<0);
 assert(comparePublishedVersions("v0.1.59-test.1","0.1.59")<0);
 assert(comparePublishedVersions("v0.1.59-test.10","v0.1.59-test.2")>0);
 assert(comparePublishedVersions("v0.1.59-test.1","0.1.59-test.1") == 0);
 assert(comparePublishedVersions("0.1.59+build.2","0.1.59+build.3") == 0);
 assert(comparePublishedVersions("0.1.59","0.1.60-test.1")<0);
}

