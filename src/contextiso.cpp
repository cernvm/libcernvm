/**
 * This file is part of CernVM Web API Plugin.
 *
 * CVMWebAPI is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CVMWebAPI is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CVMWebAPI. If not, see <http://www.gnu.org/licenses/>.
 *
 * Developed by Ioannis Charalampidis 2013
 * Contact: <ioannis.charalampidis[at]cern.ch>
 */

#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include "iso9660.h"

// CD-ROM size for 2KiB of data
static const int CONTEXTISO_CDROM_SIZE = 358400;
const char * LIBCONTEXTISO_APP = "LIBCONTEXTISO - A TINY ISO 9660-COMPATIBLE FILESYSTEM CREATOR LIBRARY (C) 2012  I.CHARALAMPIDIS                                 ";

/**
 * Hard-coded offsets
 */
static const int PRIMARY_DESCRIPTOR_OFFSET          = 0x8000;
static const int SECONDARY_DIRECTORY_RECORD_OFFSET  = 0xB800;
static const int CONTENTS_OFFSET                    = 0xC000;

static const int README_CONTENT_OFFSET = 0xE000;
// /ec2/latest/meta-data.json
static const int EC_META_CONTENT_OFFSET = 0xE800;
// /ec2/latest/user-data
static const int EC_USER_CONTENT_OFFSET = 0xF000;
// /openstack/latest/meta_data.json
static const int OS_META_CONTENT_OFFSET = 0xF800;
// /openstack/latest/user_data.json
static const int OS_USER_CONTENT_OFFSET = 0x10000;

// If you enlarge the readme content, don't forget to check the offset values above, so
// it doesn't interfere with other sections. You might also need to change the extent values (where
// the records start. Right now, we change it only for openstack meta and user data during runtime.
// Extent values for readme and ec2 meta/user data are hardcoded in the AT_* templates.
const char* README_CONTENT = \
"We support two ways of contextualization: through amiconfig and cloud init.\n"
"Amiconfig and cloud-init pick up the (same) user-data from different paths.\n"
"\n"
"Amiconfig:\n"
"Amiconfig data are put (in the plaintext format) to the \"/ec2/latest/user-data\" file.\n"
"\"ec2/latest/meta-data.json\" contains only an empty dictionary.\n"
"\n"
"Cloud init:\n"
"Cloud init data are put (in the plaintext format) to the \"/openstack/latest/user_data\" file.\n"
"\"/openstack/latest/meta_data.json\" contains only an empty dictionary.\n"
;

// If you enlarge the meta data content, don't forget to check the offset values above, so
// it doesn't interfere with other sections.
const char* META_DATA_CONTENT = \
"{\n"
"    \"uuid\": \"83679162-1378-4288-a2d4-70e13ec132aa\"\n"
"}\n"
;

/**
 * Some locally-used constants
 */
const char dateZero[] = { 0x30,0x30,0x30,0x30, 0x30,0x30, 0x30,0x30, 0x30,0x30, 0x30,0x30, 0x30,0x30, 0x30,0x30, 0x30 };

/**
 * Update a long in ISO9660 pivot-endian representation
 */
void isosetl( int x, unsigned char buffer[]) {
    buffer[0] =  x        & 0xFF;
    buffer[1] = (x >>  8) & 0xFF;
    buffer[2] = (x >> 16) & 0xFF;
    buffer[3] = (x >> 24) & 0xFF;
    buffer[4] = (x >> 24) & 0xFF;
    buffer[5] = (x >> 16) & 0xFF;
    buffer[6] = (x >>  8) & 0xFF;
    buffer[7] =  x        & 0xFF;
}


char * build_ami_ci_cdrom( const char * volume_id, const char * content, int contentSize ) {
    // Local variables
    iso_primary_descriptor  descPrimary;
    time_t rawTimeNow;
    struct tm * tmNow;

    // Prepare primary record
    memset(&descPrimary, 0, sizeof(iso_primary_descriptor));
#pragma warning(suppress: 6386)
    memset(&descPrimary.volume_set_id[0], 0x20, 1205); // Reaches till .unused5
    memset(&descPrimary.file_structure_version[0], 1, 1);
    memset(&descPrimary.unused4[0], 0, 1);

    // Copy defaults to the primary sector descriptor
    memcpy(&descPrimary, PRIMARY_DESCRIPTOR, PRIMARY_DESCRIPTOR_SIZE);

    // Build the current date
    static char dateNow[18];
    time(&rawTimeNow);
    tmNow = gmtime(&rawTimeNow);
    sprintf(&dateNow[0], "%04u%02u%02u%02u%02u%02u000", // <-- Last 0x30 ('0') is GMT Timezone
            tmNow->tm_year + 1900,
            tmNow->tm_mon,
            tmNow->tm_mday,
            tmNow->tm_hour,
            tmNow->tm_min,
            tmNow->tm_sec
        );

    // Set date fields
    memcpy(&descPrimary.creation_date[0], dateNow, 17);
    memcpy(&descPrimary.modification_date[0], dateNow, 17);
    memcpy(&descPrimary.effective_date[0], dateNow, 17);
    memcpy(&descPrimary.expiration_date[0], dateZero, 17);
    memcpy(&descPrimary.application_id, LIBCONTEXTISO_APP, 127);

    // Update volume_id on the primary sector
    int lVol = strlen(volume_id);
    if (lVol > 31)
        lVol = 31;
    memset(&descPrimary.volume_id, 0x20, 31);
    memcpy(&descPrimary.volume_id, volume_id, lVol);

    // Limit the data size to the size of the 350Kb CD-ROM buffer (that's 302Kb of data)
    size_t metaDataContentSize = strlen(META_DATA_CONTENT);
    size_t readmeSize = strlen(README_CONTENT);

    int lDataSize = 2*contentSize + 2*metaDataContentSize + readmeSize; //2 context files, 2 meta data size, 1 readme
    if (lDataSize > (CONTEXTISO_CDROM_SIZE - CONTENTS_OFFSET))
        lDataSize = CONTEXTISO_CDROM_SIZE - CONTENTS_OFFSET;

    // Calculate the volume size
    int lVolSize = 1 + ((lDataSize - 1) / 2048);
    isosetl(lVolSize, (unsigned char *)descPrimary.volume_space_size);

    // Our buffer, we can return it, because it's static and doesn't get destroyed
    static char bytes[CONTEXTISO_CDROM_SIZE];
    memset(&bytes, 0, CONTEXTISO_CDROM_SIZE);

#pragma warning(suppress: 6385)
    // Write primary descriptor
    memcpy(&bytes[PRIMARY_DESCRIPTOR_OFFSET], &descPrimary, sizeof(iso_primary_descriptor));
    memcpy(&bytes[0x8800], &ISO9660_AT_8800, ISO9660_AT_8800_SIZE);

    // Write default directory entries
    memcpy(&bytes[0x9800], &AT_9800, AT_9800_SIZE);
    memcpy(&bytes[0xA800], &AT_A800, AT_A800_SIZE);
    memcpy(&bytes[0xB800], &AT_B800, AT_B800_SIZE);
    memcpy(&bytes[0xC000], &AT_C000, AT_C000_SIZE);
    memcpy(&bytes[0xC800], &AT_C800, AT_C800_SIZE);
    memcpy(&bytes[0xD000], &AT_D000, AT_D000_SIZE);
    memcpy(&bytes[0xD800], &AT_D800, AT_D800_SIZE);

    // Recompute the offsets, if the content is too large
    int osMetaOffset = OS_META_CONTENT_OFFSET;
    int osUserOffset = OS_USER_CONTENT_OFFSET;

    if (EC_USER_CONTENT_OFFSET + contentSize >= osMetaOffset) {
        osMetaOffset = EC_USER_CONTENT_OFFSET + contentSize + 1;
        while (osMetaOffset % 2048) // align to 2kB blocks
            ++osMetaOffset;

        osUserOffset = osMetaOffset + metaDataContentSize + 1;
        while (osUserOffset % 2048) // align to 2kB blocks. This should be only one block, because metadata are smaller than one block.
            ++osUserOffset;

        // Save the new extents (on which sector the content begins)
        isosetl(osMetaOffset/2048, reinterpret_cast<unsigned char *>(&bytes[0xD800 + AT_D800_META_EXTENT_OFFSET]));
        isosetl(osUserOffset/2048, reinterpret_cast<unsigned char *>(&bytes[0xD800 + AT_D800_USER_EXTENT_OFFSET]));
    }

    // Copy the contents
    // readme
    memcpy(&bytes[README_CONTENT_OFFSET], README_CONTENT, readmeSize);
    // ec2 meta-data
    memcpy(&bytes[EC_META_CONTENT_OFFSET], META_DATA_CONTENT, metaDataContentSize);
    // ec2 user-data
    memcpy(&bytes[EC_USER_CONTENT_OFFSET], content, contentSize);
    // openstack meta_data
    memcpy(&bytes[osMetaOffset], META_DATA_CONTENT, metaDataContentSize);
    // openstack user_data
    memcpy(&bytes[osUserOffset], content, contentSize);

    // write correct file sizes
    // readme file
    isosetl(readmeSize, reinterpret_cast<unsigned char *>(&bytes[0xB800 + AT_B800_README_SIZE_OFFSET]));
    // ec2 meta data size
    isosetl(metaDataContentSize, reinterpret_cast<unsigned char *>(&bytes[0xC800 + AT_C800_META_DATA_SIZE_OFFSET]));
    // ec2 user data size
    isosetl(contentSize, reinterpret_cast<unsigned char *>(&bytes[0xC800 + AT_C800_USER_DATA_SIZE_OFFSET]));
    // openstack meta data size
    isosetl(metaDataContentSize, reinterpret_cast<unsigned char *>(&bytes[0xD800 + AT_D800_META_DATA_SIZE_OFFSET]));
    // openstack user data size
    isosetl(contentSize, reinterpret_cast<unsigned char *>(&bytes[0xD800 + AT_D800_USER_DATA_SIZE_OFFSET]));

    // Return the built buffer
    return bytes;
}

/**
 * Generate a CD-ROM ISO buffer compatible with the ISO9660 (CDFS) filesystem
 * (Using reference from: http://users.telenet.be/it3.consultants.bvba/handouts/ISO9960.html)
 */
char * build_simple_cdrom( const char * volume_id, const char * filename, const char * buffer, int size ) {

    // Local variables
    iso_primary_descriptor  descPrimary;
    iso_directory_record    descFile;
    time_t rawTimeNow;
    struct tm * tmNow;
    
    // Prepare primary record
    memset(&descPrimary, 0, sizeof(iso_primary_descriptor));
#pragma warning(suppress: 6386)
    memset(&descPrimary.volume_set_id[0], 0x20, 1205); // Reaches till .unused5
    memset(&descPrimary.file_structure_version[0], 1, 1);
    memset(&descPrimary.unused4[0], 0, 1);
    
    // Copy defaults to the primary sector descriptor
    memcpy(&descPrimary, ISO9660_PRIMARY_DESCRIPTOR, ISO9660_PRIMARY_DESCRIPTOR_SIZE);
    
    // Build the current date
    static char dateNow[18];
    time(&rawTimeNow);
    tmNow = gmtime(&rawTimeNow);
    sprintf(&dateNow[0], "%04u%02u%02u%02u%02u%02u000", // <-- Last 0x30 ('0') is GMT Timezone
            tmNow->tm_year + 1900,
            tmNow->tm_mon,
            tmNow->tm_mday,
            tmNow->tm_hour,
            tmNow->tm_min,
            tmNow->tm_sec
        );
        
    // Set date fields
    memcpy(&descPrimary.creation_date[0], dateNow, 17);
    memcpy(&descPrimary.modification_date[0], dateNow, 17);
    memcpy(&descPrimary.effective_date[0], dateNow, 17);
    memcpy(&descPrimary.expiration_date[0], dateZero, 17);
    memcpy(&descPrimary.application_id, LIBCONTEXTISO_APP, 127);
    
    // Update volume_id on the primary sector
    int lVol = strlen(volume_id);
    if (lVol > 31) lVol=31;
    memset(&descPrimary.volume_id, 0x20, 31);
    memcpy(&descPrimary.volume_id, volume_id, lVol);

    // Limit the data size to the size of the 350Kb CD-ROM buffer (that's 302Kb of data)
    int lDataSize = size;
    if (lDataSize > (CONTEXTISO_CDROM_SIZE - CONTENTS_OFFSET))
        lDataSize = CONTEXTISO_CDROM_SIZE - CONTENTS_OFFSET;
    
    // Calculate the volume size
    int lVolSize = 1 + ((lDataSize - 1) / 2048);
    isosetl(lVolSize, (unsigned char *)descPrimary.volume_space_size);
    
    // Copy defaults to CONTEXT.SH record
    memcpy(&descFile, &ISO9660_CONTEXT_SH_STRUCT[0x44], sizeof(iso_directory_record));
    
    // Update CONTEXT.SH record
    isosetl(lDataSize, descFile.size);      // <-- File size
    size_t i=0; for (;i<strlen(filename);i++) {    // <-- File name
        if (i>=10) break;
        char c = toupper(filename[i]);
        if (c==' ') c='_';
        descFile.name[i]=c;
    }
    descFile.name[i]=';';  // Add file revision (;1)
    descFile.name[i+1]='1';
    
    // Compose the CD-ROM Disk buffer
    static char bytes[CONTEXTISO_CDROM_SIZE];
    memset(&bytes,0,CONTEXTISO_CDROM_SIZE);
#pragma warning(suppress: 6385)
    memcpy(&bytes[PRIMARY_DESCRIPTOR_OFFSET],           &descPrimary,               sizeof(iso_primary_descriptor));
    memcpy(&bytes[0x8800],                              &ISO9660_AT_8800,           ISO9660_AT_8800_SIZE);
    memcpy(&bytes[0x9800],                              &ISO9660_AT_9800,           ISO9660_AT_9800_SIZE);
    memcpy(&bytes[0xA800],                              &ISO9660_AT_A800,           ISO9660_AT_A800_SIZE);
    memcpy(&bytes[SECONDARY_DIRECTORY_RECORD_OFFSET],   &ISO9660_CONTEXT_SH_STRUCT, ISO9660_CONTEXT_SH_STRUCT_SIZE);
    memcpy(&bytes[SECONDARY_DIRECTORY_RECORD_OFFSET+68],&descFile,                  sizeof(iso_directory_record));
    memcpy(&bytes[CONTENTS_OFFSET],                     buffer,                     lDataSize);
    
    // Return the built buffer
    return bytes;
}
