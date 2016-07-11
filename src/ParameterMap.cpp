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

#include <CernVM/CrashReport.h>
#include <CernVM/Utilities.h>
#include <CernVM/ParameterMap.h>

const std::string SAFE_KEY_CHARS("0123456789_-abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");

/**
 * Helper to replace key on the const map
 */
void ParameterMap::putOnMap( ParameterDataMapPtr map, const std::string& key, const std::string& value) {
    std::map< const std::string, const std::string >::iterator it = map->find(key);
    if (it != map->end()) map->erase(it);
    map->insert(std::make_pair(key, value));
}

/**
 * Allocate a new shared pointer
 */
ParameterMapPtr ParameterMap::instance ( ) {
    CRASH_REPORT_BEGIN;
    return boost::make_shared< ParameterMap >();
    CRASH_REPORT_END;
}

/**
 * Return a string parameter value
 */
std::string ParameterMap::get( const std::string& kname, std::string defaultValue, bool strict ) {
    CRASH_REPORT_BEGIN;
    // Replace invalid chars in strict mode
    std::string name = kname;
    if (strict) {
        for (size_t i=0; i<name.length(); i++) {
            if (SAFE_KEY_CHARS.find(name[i]) == std::string::npos)
                name[i] = '_';
        }
    }
    // Append prefix
    name = prefix + name;
    {
        // Mutex for thread-safety
        boost::unique_lock<boost::mutex> lock(*parametersMutex);
        if (parameters->find(name) == parameters->end())
            return defaultValue;
        return (*parameters)[name];
    }
    CRASH_REPORT_END;
}

/**
 * Set a string parameter
 */
ParameterMap& ParameterMap::set ( const std::string& kname, const std::string value ) {
    CRASH_REPORT_BEGIN;
    std::string name = prefix + kname;
    
    {
        // Mutex for thread-safety
        boost::unique_lock<boost::mutex> lock(*parametersMutex);
        putOnMap(parameters, name, value);
    }

    if (!locked) {
        commitChanges();
    } else {
        changed = true;
    }
    return *this;
    CRASH_REPORT_END;
}

/**
 * Delete a parameter
 */
ParameterMap& ParameterMap::erase ( const std::string& name ) {
    CRASH_REPORT_BEGIN;
    {
        // Mutex for thread-safety
        boost::unique_lock<boost::mutex> lock(*parametersMutex);
        std::map<const std::string, const std::string>::iterator e = parameters->find(prefix+name);
        if (e != parameters->end())
            parameters->erase(e);
    }
    return *this;
    CRASH_REPORT_END;
}

/**
 * Set a string parameter only if the value is missing.
 * This does not trigger the commitChanges function.
 */
void ParameterMap::setDefault ( const std::string& kname, std::string value ) {
    CRASH_REPORT_BEGIN;
    std::string name = prefix + kname;

    // Insert the given entry (only if it's missing)
    // and don't trigger commitChanges.
    {
        // Mutex for thread-safety
        boost::unique_lock<boost::mutex> lock(*parametersMutex);
        parameters->insert(std::pair< const std::string, const std::string >( name, value ));
    }

    CRASH_REPORT_END;
}

/**
 * Get a numeric parameter value
 */
template<typename T> T ParameterMap::getNum ( const std::string& kname, T defaultValue ) {
    CRASH_REPORT_BEGIN;
    std::string name = prefix + kname;
    {
        // Mutex for thread-safety
        boost::unique_lock<boost::mutex> lock(*parametersMutex);
        if (parameters->find(name) == parameters->end())
            return defaultValue;
        return ston<T>((*parameters)[name]);
    }
    CRASH_REPORT_END;
}

/**
 * Set a numeric parameter value
 */
template<typename T> ParameterMap& ParameterMap::setNum ( const std::string& kname, T value ) {
    CRASH_REPORT_BEGIN;
    set( kname, ntos<T>(value) );
    return *this;
    CRASH_REPORT_END;
}

/**
 * Empty the parameter set
 */
ParameterMap& ParameterMap::clear( ) {
    CRASH_REPORT_BEGIN;

    // Get the keys for this group
    std::vector<std::string> myKeys = enumKeys();

    // Delete keys
    {
        // Mutex for thread-safety
        boost::unique_lock<boost::mutex> lock(*parametersMutex);
        for (std::vector<std::string>::iterator it = myKeys.begin(); it != myKeys.end(); ++it) {
            parameters->erase( prefix + *it );
        }
    }

    return *this;
    CRASH_REPORT_END;
}

/**
 * Empty all the parameter set
 */
ParameterMap& ParameterMap::clearAll( ) {
    CRASH_REPORT_BEGIN;
    {
        // Mutex for thread-safety
        boost::unique_lock<boost::mutex> lock(*parametersMutex);
        parameters->clear();
    }
    return *this;
    CRASH_REPORT_END;
}

/**
 * Lock the parameter map disable updates
 */
ParameterMap& ParameterMap::lock ( ) {
    CRASH_REPORT_BEGIN;
    locked = true;
    changed = false;
    return *this;
    CRASH_REPORT_END;
}

/**
 * Unlock the parameter map and enable updates
 */
ParameterMap& ParameterMap::unlock ( ) {
    CRASH_REPORT_BEGIN;
    locked = false;
    if (changed) commitChanges();
    return *this;
    CRASH_REPORT_END;
}

/**
 * Locally overridable function to commit changes to the dictionary
 */
void ParameterMap::commitChanges ( ) {
    CRASH_REPORT_BEGIN;

    // Forward event to parent
    if (parent) parent->commitChanges();
    
    CRASH_REPORT_END;
}

/**
 * Return a sub-parameter group instance
 */
ParameterMapPtr ParameterMap::subgroup( const std::string& kname ) {
    CRASH_REPORT_BEGIN;

    // Calculate the prefix of the sub-group
    std::string name = prefix + kname + PMAP_GROUP_SEPARATOR;

    // Return a new ParametersMap instance that encapsulate us
    // as parent.
    return boost::make_shared<ParameterMap>( shared_from_this(), name );

    CRASH_REPORT_END;
}

/**
 * Enumerate the variable names that match our current prefix
 */
std::vector<std::string > ParameterMap::enumKeys ( ) {
    CRASH_REPORT_BEGIN;
    std::vector<std::string > keys;

    // Loop over the entries in the record
    {
        // Mutex for thread-safety
        boost::unique_lock<boost::mutex> lock(*parametersMutex);
        for ( std::map<const std::string, const std::string>::iterator it = parameters->begin(); it != parameters->end(); ++it ) {
            std::string key = (*it).first;

            // Check for matching prefix and lack of group separator in the rest of the key
            if ( (key.substr(0, prefix.length()).compare(prefix) == 0) && // Prefix Matches
                    (key.substr(prefix.length(), key.length() - prefix.length() ).find(PMAP_GROUP_SEPARATOR) == std::string::npos) // No group separator found
                ) {

                // Store key name without prefix
                keys.push_back( key.substr(prefix.length(), key.length() - prefix.length()) ); 
            }

        }
    }

    // Return the keys vector
    return keys;

    CRASH_REPORT_END;
}

/**
 * Return true if the specified parameter exists
 */
bool ParameterMap::contains ( const std::string& name, const bool useBlank ) {
    CRASH_REPORT_BEGIN;
    // Mutex for thread-safety
    boost::unique_lock<boost::mutex> lock(*parametersMutex);

    bool ans = (parameters->find(prefix + name) != parameters->end());
    if ( ans && useBlank ) {
        return !(*parameters)[prefix+name].empty();
    } else {
        return ans;
    }
    CRASH_REPORT_END;
}

/**
 * Return false if a parameter was completely erased
 */
bool ParameterMap::filterParameter ( const std::string& parameter ) {
    CRASH_REPORT_BEGIN;

    if (! this->contains(parameter)) {
        CVMWA_LOG("Debug", "filterParameter: given parameter is not inside the map: " << parameter);
        return true;
    }

    std::string value = this->get(parameter);
    bool valueWithInvalidChar = false;

    //check if the value contains illegal characters, if so, erase it
    for (std::string::iterator it = value.begin(); it != value.end(); ) {
        if (SAFE_KEY_CHARS.find(*it) == std::string::npos) {
            valueWithInvalidChar = true;
            it = value.erase(it);
        }
        else
            ++it;
    }

    //save the new value if necessary
    if (valueWithInvalidChar) {
        CVMWA_LOG("Debug", "Filtered parameter's ('" << parameter << "') value from '" << this->get(parameter) << "' to: '" << value << "'");
        {
            // Mutex for thread-safety
            boost::unique_lock<boost::mutex> lock(*parametersMutex);
            putOnMap(this->parameters, prefix+parameter, value);
        }

        // If we are not locked, sync changes.
        // Oherwise mark us as dirty
        if (!locked) {
            commitChanges();
        } else {
            changed = true;
        }
        if (value.empty()) return false; //value was made only from illegal chars
    }

    return true;

    CRASH_REPORT_END;
}

/**
 * Update all the parameters from the specified map
 */
void ParameterMap::fromParameters ( const ParameterMapPtr& ptr, bool clearBefore, const bool replace ) {
    CRASH_REPORT_BEGIN;

    // Check if we have to clean the keys first
    if (clearBefore) clear();

    // Get parameter keys
    std::vector< std::string > ptrKeys = ptr->enumKeys();

    // Update parameters
    std::string k;
    for (std::vector< std::string >::iterator it = ptrKeys.begin(); it != ptrKeys.end(); ++it) {
        CVMWA_LOG("INFO", "Importing key " << *it << " = " << ptr->parameters->at(*it));
        k = prefix + *it;
        if (replace || (this->parameters->find(k) == this->parameters->end())) {
            // Mutex for thread-safety
            boost::unique_lock<boost::mutex> lock(*parametersMutex);
            putOnMap(this->parameters, k, (*ptr->parameters)[*it]);
        }
    }

    // If we are not locked, sync changes.
    // Oherwise mark us as dirty
    if (!locked) {
        commitChanges();
    } else {
        changed = true;
    }

    CRASH_REPORT_END;
}

/**
 * Update all the parameters from the specified map
 */
void ParameterMap::fromMap ( std::map< const std::string, const std::string> * map, bool clearBefore, const bool replace ) {
    CRASH_REPORT_BEGIN;

    // Check if we have to clean the keys first
    if (clearBefore) clear();

    // Store values
    if (map == NULL) return;
    {
        // Mutex for thread-safety
        boost::unique_lock<boost::mutex> lock(*parametersMutex);

        // Update parameters
        std::string k;
        for (std::map< const std::string, const std::string>::iterator it = map->begin(); it != map->end(); ++it) {
            k = prefix + (*it).first;
            if (replace || (parameters->find(k) == parameters->end()))
            putOnMap(this->parameters, k, (*it).second);
        }
    }

    // If we are not locked, sync changes.
    // Oherwise mark us as dirty
    if (!locked) {
        commitChanges();
    } else {
        changed = true;
    }

    CRASH_REPORT_END;
}

/**
 * Update all the parameters from the specified JSON Value
 */
void ParameterMap::fromJSON( const Json::Value& json, bool clearBefore, const bool replace ){
    CRASH_REPORT_BEGIN;

    // Check if we have to clean the keys first
    if (clearBefore) clear();

    // Update parameters
    const Json::Value::Members membNames = json.getMemberNames();
    for (std::vector<std::string>::const_iterator it = membNames.begin(); it != membNames.end(); ++it) {
        std::string k = *it;
        Json::Value v = json[k];
        if (v.isObject()) {
            ParameterMapPtr sg = subgroup(k);
            sg->fromJSON(v);
        } else if (v.isString()) {
            if (replace || (parameters->find(k) == parameters->end())) {
                // Mutex for thread-safety
                boost::unique_lock<boost::mutex> lock(*parametersMutex);
                putOnMap(this->parameters, k, v.asString());
            }
        } else if (v.isInt()) {
            int vv = v.asInt();
            if (replace || (parameters->find(k) == parameters->end())) {
                // Mutex for thread-safety
                boost::unique_lock<boost::mutex> lock(*parametersMutex);
                putOnMap(this->parameters, k, ntos<int>( vv ));
            }
        }
    }

    // If we are not locked, sync changes.
    // Oherwise mark us as dirty
    if (!locked) {
        commitChanges();
    } else {
        changed = true;
    }

    CRASH_REPORT_END;
}

/**
 * Store all the parameters to the specified map
 */
void ParameterMap::toMap ( std::map< const std::string, const std::string> * map, bool clearBefore ) {
    CRASH_REPORT_BEGIN;

    // Clear map
    if (clearBefore) {
        // Mutex for thread-safety
        boost::unique_lock<boost::mutex> lock(*parametersMutex);
        // Clear
        map->clear();
    }

    // Get the keys for this group
    std::vector<std::string> myKeys = enumKeys();

    // Read parameters
    for (std::vector<std::string>::iterator it = myKeys.begin(); it != myKeys.end(); ++it) {
        map->insert(std::pair< const std::string, const std::string >( (const std::string)*it, (*parameters)[ prefix + *it] ));
    }

    CRASH_REPORT_END;
}

/**
 * Set a boolean parameter
 */
ParameterMap& ParameterMap::setBool ( const std::string& name, bool value ) {
    CRASH_REPORT_BEGIN;
    std::string v = "n";
    if (value) v = "y";
    set(name, v);
    return *this;
    CRASH_REPORT_END;
}

/**
 * Get a boolean parameter
 */
bool ParameterMap::getBool ( const std::string& name, bool defaultValue ) {
    CRASH_REPORT_BEGIN;
    std::string v = get(name, "");
    if (v.empty()) return defaultValue;
    return ((v[0] == 'y') || (v[0] == 't') || (v[0] == '1'));
    CRASH_REPORT_END;
}

/**
 * Synchronize file contents with the dictionary contents
 */
bool ParameterMap::sync ( ) {
    CRASH_REPORT_BEGIN;

    // If we have parent, forward to the root element
    if (parent) {
        return parent->sync();
    }

    // Otherwise succeed - we can't sync anything
    // one way or another.
    return true;

    CRASH_REPORT_END;
}

/**
 * Template implementations for numeric values
 */
template int ParameterMap::getNum<int>( const std::string&, int defValue );
template ParameterMap& ParameterMap::setNum<int>( const std::string&, int value );
template long ParameterMap::getNum<long>( const std::string&, long defValue );
template ParameterMap& ParameterMap::setNum<long>( const std::string&, long value );
