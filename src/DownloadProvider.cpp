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

#include "CernVM/DownloadProvider.h"
#include "CernVM/Hypervisor.h"

DownloadProviderPtr systemProvider;

/**
 * Get system-wide download provider singleton
 */
DownloadProviderPtr DownloadProvider::Default() {
    CRASH_REPORT_BEGIN;
    if (!systemProvider)
        systemProvider = boost::make_shared< CURLProvider >();
    return systemProvider;
    CRASH_REPORT_END;
}

/**
 * Set system-wide default provider singleton
 */
void DownloadProvider::setDefault( const DownloadProviderPtr& provider ) {
    CRASH_REPORT_BEGIN;
    systemProvider = provider;
    CRASH_REPORT_END;
}

/**
 * Local function to fire the progress event accordingly
 */
void DownloadProvider::fireProgressEvent( const VariableTaskPtr& fb, size_t pos, size_t max ) {
    CRASH_REPORT_BEGIN;
    if (fb) {
        
        // Throttle events on 2 per second, unless that's the last one
        if ((pos != max) && ((getMillis() - fb->__lastEventTime) < DP_THROTTLE_TIMER)) return;
        fb->__lastEventTime = getMillis();

        // Update task's progress
        CVMWA_LOG("Debug", "Updating progress to " << pos << "/" << max );
        fb->setMax(max);
        fb->update(pos);

    }
    CRASH_REPORT_END;
}

/**
 * Local function to write data to osstream
 */
void DownloadProvider::writeToStream( std::ostream * stream, const VariableTaskPtr& fb, long max_size, const char * ptr, size_t data ) {
    CRASH_REPORT_BEGIN;
    
    // Write data to the file
    stream->write( ptr, data );
    
    // Update progress
    if (max_size != 0) {
        
        // Get position inside the stream
        size_t cur_size = stream->tellp();
        
        // Fire progress update
        if (fb)
            DownloadProvider::fireProgressEvent( fb, cur_size, max_size );
    }

    CRASH_REPORT_END;
}

/**
 * Extract the content-length from function
 */
size_t __curl_headerfunc( void *ptr, size_t size, size_t nmemb, CURLProvider * self) {
    CRASH_REPORT_BEGIN;
    size_t dataLen = size * nmemb;
    
    // Move data to std::String
    std::string cppString( (char *) ptr, dataLen );
    if ((cppString.length() > 16) &&  (cppString.substr(0,16).compare("Content-Length: ") == 0)) {
        cppString = cppString.substr(16, cppString.length()-18 );
        CVMWA_LOG("Debug", "Found Content-Length: '" << cppString << "'");
        self->maxStreamSize = ston<size_t>( cppString );
    }
    
    return dataLen;
    CRASH_REPORT_END;
}

/**
 * Callback function for CURL data
 */
size_t __curl_datacb_file(void *ptr, size_t size, size_t nmemb, CURLProvider * self ) {
    CRASH_REPORT_BEGIN;
    size_t dataLen = size * nmemb;

    //CVMWA_LOG("Debug", "cURL File callback (size=" << dataLen << ")");

    // Write to file stream
    DownloadProvider::writeToStream( &(self->fStream), self->pf, self->maxStreamSize, (const char *) ptr, dataLen );
    
    // Return data len
    return dataLen;
    CRASH_REPORT_END;
}

/**
* Callback function for CURL data
 */
size_t __curl_datacb_string(void *ptr, size_t size, size_t nmemb, CURLProvider * self ) {
    CRASH_REPORT_BEGIN;
    size_t dataLen = size * nmemb;

    CVMWA_LOG("Debug", "cURL String callback (size=" << dataLen << ")");

    // Write to string stream
    DownloadProvider::writeToStream( &(self->sStream), self->pf, self->maxStreamSize, (const char *) ptr, dataLen );

    // Return data len
    return dataLen;
    CRASH_REPORT_END;
}

/**
 * Callback function for checking for aborted CURL state
 */
int __curl_xferinfo(CURLProvider * self, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    CRASH_REPORT_BEGIN;

    // If we are aborted return -1
    if (self->abortFlag) {
        self->abortFlag = self->abortPersistsFlag;
        return -1;
    }

    // Return 0 to continue downlad
    return 0;

    CRASH_REPORT_END;
}

/**
 * Download a file using CURL
 */
int CURLProvider::downloadFile( const std::string& url, const std::string& destination, const VariableTaskPtr& pf ) {
    CRASH_REPORT_BEGIN;

    // We are in operation
    operationInstances++;

    // Setup CURL url
    CVMWA_LOG("Debug", "Downloading file from '" << url << "'");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // There is no way to wait for more than 10 seconds just for the connection
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L );
    // Files can be big (assume up to 10G), with the worst case of 10Mbps, it won't take more than 2h
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 7200L );

    // Store a local pointer
    this->pf = pf;
    this->maxStreamSize = 0;

    // Reset timestamp
    if (pf) pf->__lastEventTime = getMillis();
    
    // Setup callbacks
    //CURLProviderPtr sharedPtr = boost::dynamic_pointer_cast< CURLProvider >( shared_from_this() );
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, __curl_headerfunc);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, __curl_datacb_file);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, __curl_xferinfo);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this); //sharedPtr.get() );
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, this); //sharedPtr.get() );
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
    
    // Open local file
    CVMWA_LOG("Debug", "Oppening local output stream '" << destination << "'");
    fStream.clear();
    fStream.open( destination.c_str(), std::ofstream::binary );
    if (fStream.fail()) {
        CVMWA_LOG("Error", "OFStream error" );
        operationInstances--;
        return HVE_IO_ERROR;
    }
    
    // Initiate connection (we have specified CURLOPT_CONNECT_ONLY)
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        CVMWA_LOG("Error", "cURL Error #" << res );
        operationInstances--;
        return HVE_IO_ERROR;
    } else {
        CVMWA_LOG("Info", "cURL Download completed" );
    }

    // Notify completion
    if (pf) pf->complete("Download completed");
    
    // Close stream
    fStream.close();
    operationInstances--;
    return HVE_OK;
    
    CRASH_REPORT_END;
}

/**
 * Download a file using CURL
 */
int CURLProvider::downloadText( const std::string& url, std::string * destination, const VariableTaskPtr& pf ) {
    CRASH_REPORT_BEGIN;
    
    // We are in operation
    operationInstances++;

    // Setup CURL url
    CVMWA_LOG("Debug", "Downloading string from '" << url << "'");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // There is no way to wait for more than 10 seconds just for the connection
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L );
    // Texts are usually small, so we are not expecting it to take more than a minute on the slowest networks ever
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L );

    // Store a local pointer
    this->pf = pf;
    this->maxStreamSize = 0;
    
    // Reset timestamp
    if (pf) pf->__lastEventTime = getMillis();
    
    // Setup callbacks
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, __curl_headerfunc);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, __curl_datacb_string);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, __curl_xferinfo);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, this);
    
    // Reset string stream
    CVMWA_LOG("Debug", "Resetting string stream");
    sStream.clear();
    sStream.str("");
    
    // Initiate connection (we have specified CURLOPT_CONNECT_ONLY)
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        CVMWA_LOG("Error", "cURL Error #" << res );
        sStream.str("");
        operationInstances--;
        return HVE_IO_ERROR;
    }
    
    // Copy to output and clear buffer
    *destination = sStream.str();
    sStream.str("");

    // Notify completion
    if (pf) pf->complete("Download completed");

    CVMWA_LOG("Info", "cURL Download completed" );
    operationInstances--;
    return HVE_OK;
    
    CRASH_REPORT_END;
}

/**
 * Create a clone of this instance
 */
DownloadProviderPtr CURLProvider::clone() {
    // Just return a new CURL instance
    return boost::make_shared< CURLProvider >();
}

/**
 * Abort a single file transfer
 */
int CURLProvider::abort() {
    CRASH_REPORT_BEGIN;
    // Abort if there is anything pending
    if (operationInstances>0) {
        abortFlag = true;
        abortPersistsFlag = false;
    }
    return HVE_OK;
    CRASH_REPORT_END;
}

/**
 * Block all possible file transfers
 */
int CURLProvider::abortAll() {
    CRASH_REPORT_BEGIN;
    abortFlag = true;
    abortPersistsFlag = true;
    return HVE_OK;
    CRASH_REPORT_END;
}

