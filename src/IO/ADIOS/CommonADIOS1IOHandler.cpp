/* Copyright 2017-2018 Fabian Koller, Axel Huebl
 *
 * This file is part of openPMD-api.
 *
 * openPMD-api is free software: you can redistribute it and/or modify
 * it under the terms of of either the GNU General Public License or
 * the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * openPMD-api is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License and the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and the GNU Lesser General Public License along with openPMD-api.
 * If not, see <http://www.gnu.org/licenses/>.
 */

void
CommonADIOS1IOHandlerImpl::close(int64_t fd)
{
    int status;
    status = adios_close(fd);
    VERIFY(status == err_no_error, "Internal error: Failed to open_write close ADIOS file during creation");
}

void
CommonADIOS1IOHandlerImpl::close(ADIOS_FILE* f)
{
    int status;
    status = adios_read_close(f);
    VERIFY(status == err_no_error, "Internal error: Failed to close ADIOS file");
}

void
CommonADIOS1IOHandlerImpl::createFile(Writable* writable,
                                Parameter< Operation::CREATE_FILE > const& parameters)
{
    if( m_handler->accessType == AccessType::READ_ONLY )
        throw std::runtime_error("Creating a file in read-only mode is not possible.");

    if( !writable->written )
    {
        if( !auxiliary::directory_exists(m_handler->directory) )
        {
            bool success = auxiliary::create_directories(m_handler->directory);
            VERIFY(success, "Internal error: Failed to create directories during ADIOS file creation");
        }

        std::string name = m_handler->directory + parameters.name;
        if( !auxiliary::ends_with(name, ".bp") )
            name += ".bp";

        writable->written = true;
        writable->abstractFilePosition = std::make_shared< ADIOS1FilePosition >("/");

        m_filePaths[writable] = std::make_shared< std::string >(name);

        /* defer actually opening the file handle until the first Operation::WRITE_DATASET occurs */
        m_existsOnDisk[m_filePaths[writable]] = false;
    }
}

void
CommonADIOS1IOHandlerImpl::createPath(Writable* writable,
                                Parameter< Operation::CREATE_PATH > const& parameters)
{
    if( m_handler->accessType == AccessType::READ_ONLY )
        throw std::runtime_error("Creating a path in a file opened as read only is not possible.");

    if( !writable->written )
    {
        /* Sanitize path */
        std::string path = parameters.path;
        if( auxiliary::starts_with(path, '/') )
            path = auxiliary::replace_first(path, "/", "");
        if( !auxiliary::ends_with(path, '/') )
            path += '/';

        /* ADIOS has no concept for explicitly creating paths.
         * They are implicitly created with the paths of variables/attributes. */

        writable->written = true;
        writable->abstractFilePosition = std::make_shared< ADIOS1FilePosition >(path);

        Writable* position;
        if( writable->parent )
            position = writable->parent;
        else
            position = writable; /* root does not have a parent but might still have to be written */
        auto res = m_filePaths.find(position);

        m_filePaths[writable] = res->second;
    }
}

void
CommonADIOS1IOHandlerImpl::createDataset(Writable* writable,
                                   Parameter< Operation::CREATE_DATASET > const& parameters)
{
    if( m_handler->accessType == AccessType::READ_ONLY )
        throw std::runtime_error("Creating a dataset in a file opened as read only is not possible.");

    if( !writable->written )
    {
        /* ADIOS variable definitions require the file to be (re-)opened to take effect/not cause errors */
        auto res = m_filePaths.find(writable);
        if( res == m_filePaths.end() )
            res = m_filePaths.find(writable->parent);
        auto it = m_openWriteFileHandles.find(res->second);
        if( it != m_openWriteFileHandles.end() )
        {
            close(m_openWriteFileHandles.at(res->second));
            m_openWriteFileHandles.erase(it);
        }

        /* Sanitize name */
        std::string name = parameters.name;
        if( auxiliary::starts_with(name, '/') )
            name = auxiliary::replace_first(name, "/", "");
        if( auxiliary::ends_with(name, '/') )
            name = auxiliary::replace_first(name, "/", "");

        std::string path = concrete_bp1_file_position(writable) + name;

        size_t ndims = parameters.extent.size();

        std::vector< std::string > chunkSize(ndims, "");
        std::vector< std::string > chunkOffset(ndims, "");
        int64_t id;
        for( size_t i = 0; i < ndims; ++i )
        {
            chunkSize[i] = "/tmp" + path + "_chunkSize" + std::to_string(i);
            id = adios_define_var(m_group, chunkSize[i].c_str(), "", adios_unsigned_long, "", "", "");
            VERIFY(id != 0, "Internal error: Failed to define ADIOS variable during Dataset creation");
            chunkOffset[i] = "/tmp" + path + "_chunkOffset" + std::to_string(i);
            id = adios_define_var(m_group, chunkOffset[i].c_str(), "", adios_unsigned_long, "", "", "");
            VERIFY(id != 0, "Internal error: Failed to define ADIOS variable during Dataset creation");
        }

        std::string chunkSizeParam = auxiliary::join(chunkSize, ",");
        std::string globalSize = getBP1Extent(parameters.extent);
        std::string chunkOffsetParam = auxiliary::join(chunkOffset, ",");
        id = adios_define_var(m_group,
                              path.c_str(),
                              "",
                              getBP1DataType(parameters.dtype),
                              chunkSizeParam.c_str(),
                              globalSize.c_str(),
                              chunkOffsetParam.c_str());
        VERIFY(id != 0, "Internal error: Failed to define ADIOS variable during Dataset creation");

        if( !parameters.compression.empty() )
            std::cerr << "Custom compression not compatible with ADIOS1 backend. Use transform instead."
                      << std::endl;

        if( !parameters.transform.empty() )
        {
            int status;
            status = adios_set_transform(id, parameters.transform.c_str());
            VERIFY(status == err_no_error, "Internal error: Failed to set ADIOS transform during Dataset cretaion");
        }

        writable->written = true;
        writable->abstractFilePosition = std::make_shared< ADIOS1FilePosition >(name);

        m_filePaths[writable] = res->second;
    }
}

void
CommonADIOS1IOHandlerImpl::extendDataset(Writable*,
                                   Parameter< Operation::EXTEND_DATASET > const&)
{
    throw std::runtime_error("Dataset extension not implemented in ADIOS backend");
}

void
CommonADIOS1IOHandlerImpl::openFile(Writable* writable,
                              Parameter< Operation::OPEN_FILE > const& parameters)
{
    if( !auxiliary::directory_exists(m_handler->directory) )
        throw no_such_file_error("Supplied directory is not valid: " + m_handler->directory);

    std::string name = m_handler->directory + parameters.name;
    if( !auxiliary::ends_with(name, ".bp") )
        name += ".bp";

    std::shared_ptr< std::string > filePath;
    if( m_filePaths.find(writable) == m_filePaths.end() )
        filePath = std::make_shared< std::string >(name);
    else
        filePath = m_filePaths[writable];

    /* close the handle that corresponds to the file we want to open */
    if( m_openWriteFileHandles.find(filePath) != m_openWriteFileHandles.end() )
    {
        close(m_openWriteFileHandles[filePath]);
        m_openWriteFileHandles.erase(filePath);
    }

    ADIOS_FILE* f = open_read(name);

    writable->written = true;
    writable->abstractFilePosition = std::make_shared< ADIOS1FilePosition >("/");

    m_openReadFileHandles[filePath] = f;
    m_filePaths[writable] = filePath;
}

void
CommonADIOS1IOHandlerImpl::openPath(Writable* writable,
                              Parameter< Operation::OPEN_PATH > const& parameters)
{
    /* Sanitize path */
    std::string path = parameters.path;
    if( auxiliary::starts_with(path, '/') )
        path = auxiliary::replace_first(path, "/", "");
    if( !auxiliary::ends_with(path, '/') )
        path += '/';

    writable->written = true;
    writable->abstractFilePosition = std::make_shared< ADIOS1FilePosition >(path);

    auto res = m_filePaths.find(writable);
    if( res == m_filePaths.end() )
        res = m_filePaths.find(writable->parent);

    m_filePaths[writable] = res->second;
}

void
CommonADIOS1IOHandlerImpl::openDataset(Writable* writable,
                                 Parameter< Operation::OPEN_DATASET >& parameters)
{
    ADIOS_FILE *f;
    auto res = m_filePaths.find(writable);
    if( res == m_filePaths.end() )
        res = m_filePaths.find(writable->parent);
    f = m_openReadFileHandles.at(res->second);

    /* Sanitize name */
    std::string name = parameters.name;
    if( auxiliary::starts_with(name, '/') )
        name = auxiliary::replace_first(name, "/", "");

    std::string datasetname = concrete_bp1_file_position(writable) + name;

    ADIOS_VARINFO* vi;
    vi = adios_inq_var(f,
                       datasetname.c_str());
    VERIFY(adios_errno == err_no_error, "Internal error: Failed to inquire about ADIOS variable during dataset opening");
    VERIFY(vi != nullptr, "Internal error: Failed to inquire about ADIOS variable during dataset opening");

    Datatype dtype;
    switch( vi->type )
    {
        using DT = Datatype;
        case adios_byte:
            dtype = DT::CHAR;
            break;
        case adios_short:
            dtype = DT::INT16;
            break;
        case adios_integer:
            dtype = DT::INT32;
            break;
        case adios_long:
            dtype = DT::INT64;
            break;
        case adios_unsigned_byte:
            dtype = DT::UCHAR;
            break;
        case adios_unsigned_short:
            dtype = DT::UINT16;
            break;
        case adios_unsigned_integer:
            dtype = DT::UINT32;
            break;
        case adios_unsigned_long:
            dtype = DT::UINT64;
            break;
        case adios_real:
            dtype = DT::FLOAT;
            break;
        case adios_double:
            dtype = DT::DOUBLE;
            break;
        case adios_long_double:
            dtype = DT::LONG_DOUBLE;
            break;

        case adios_string:
        case adios_string_array:
        case adios_complex:
        case adios_double_complex:
        default:
            throw unsupported_data_error("Datatype not implemented for ADIOS dataset writing");
    }
    *parameters.dtype = dtype;

    Extent e;
    e.resize(vi->ndim);
    for( int i = 0; i < vi->ndim; ++i )
        e[i] = vi->dims[i];
    *parameters.extent = e;

    writable->written = true;
    writable->abstractFilePosition = std::make_shared< ADIOS1FilePosition >(name);

    m_openReadFileHandles[res->second] = f;
    m_filePaths[writable] = res->second;
}

void
CommonADIOS1IOHandlerImpl::deleteFile(Writable* writable,
                                Parameter< Operation::DELETE_FILE > const& parameters)
{
    if( m_handler->accessType == AccessType::READ_ONLY )
        throw std::runtime_error("Deleting a file opened as read only is not possible.");

    if( writable->written )
    {
        auto path = m_filePaths.at(writable);
        if( m_openReadFileHandles.find(path) != m_openReadFileHandles.end() )
        {
            close(m_openReadFileHandles.at(path));
            m_openReadFileHandles.erase(path);
        }
        if( m_openWriteFileHandles.find(path) != m_openWriteFileHandles.end() )
        {
            close(m_openWriteFileHandles.at(path));
            m_openWriteFileHandles.erase(path);
        }

        std::string name = m_handler->directory + parameters.name;
        if( !auxiliary::ends_with(name, ".bp") )
            name += ".bp";

        if( !auxiliary::file_exists(name) )
            throw std::runtime_error("File does not exist: " + name);

        auxiliary::remove_file(name);

        writable->written = false;
        writable->abstractFilePosition.reset();

        m_filePaths.erase(writable);
    }
}

void
CommonADIOS1IOHandlerImpl::deletePath(Writable*,
                                Parameter< Operation::DELETE_PATH > const&)
{
    throw std::runtime_error("Path deletion not implemented in ADIOS backend");
}

void
CommonADIOS1IOHandlerImpl::deleteDataset(Writable*,
                                   Parameter< Operation::DELETE_DATASET > const&)
{
    throw std::runtime_error("Dataset deletion not implemented in ADIOS backend");
}

void
CommonADIOS1IOHandlerImpl::deleteAttribute(Writable*,
                                     Parameter< Operation::DELETE_ATT > const&)
{
    throw std::runtime_error("Attribute deletion not implemented in ADIOS backend");
}

void
CommonADIOS1IOHandlerImpl::writeDataset(Writable* writable,
                                  Parameter< Operation::WRITE_DATASET > const& parameters)
{
    if( m_handler->accessType == AccessType::READ_ONLY )
        throw std::runtime_error("Writing into a dataset in a file opened as read only is not possible.");

    /* file opening is deferred until the first dataset write to a file occurs */
    auto res = m_filePaths.find(writable);
    if( res == m_filePaths.end() )
        res = m_filePaths.find(writable->parent);
    int64_t fd;
    if( m_openWriteFileHandles.find(res->second) == m_openWriteFileHandles.end() )
    {
        fd = open_write(writable);
        m_openWriteFileHandles[res->second] = fd;
    } else
        fd = m_openWriteFileHandles.at(res->second);

    std::string name = concrete_bp1_file_position(writable);

    size_t ndims = parameters.extent.size();

    std::string chunkSize;
    std::string chunkOffset;
    int status;
    for( size_t i = 0; i < ndims; ++i )
    {
        chunkSize = "/tmp" + name + "_chunkSize" + std::to_string(i);
        status = adios_write(fd, chunkSize.c_str(), &parameters.extent[i]);
        VERIFY(status == err_no_error, "Internal error: Failed to write ADIOS variable during Dataset writing");
        chunkOffset = "/tmp" + name + "_chunkOffset" + std::to_string(i);
        status = adios_write(fd, chunkOffset.c_str(), &parameters.offset[i]);
        VERIFY(status == err_no_error, "Internal error: Failed to write ADIOS variable during Dataset writing");
    }

    status = adios_write(fd,
                         name.c_str(),
                         parameters.data.get());
    VERIFY(status == err_no_error, "Internal error: Failed to write ADIOS variable during Dataset writing");
}

void
CommonADIOS1IOHandlerImpl::writeAttribute(Writable* writable,
                                    Parameter< Operation::WRITE_ATT > const& parameters)
{
    if( m_handler->accessType == AccessType::READ_ONLY )
        throw std::runtime_error("Writing an attribute in a file opened as read only is not possible.");

    Attribute att = Attribute(parameters.resource);

    int nelems = 0;
    switch( parameters.dtype )
    {
        using DT = Datatype;
        case DT::VEC_CHAR:
            nelems = att.get< std::vector< char > >().size();
            break;
        case DT::VEC_INT16:
            nelems = att.get< std::vector< int16_t > >().size();
            break;
        case DT::VEC_INT32:
            nelems = att.get< std::vector< int32_t > >().size();
            break;
        case DT::VEC_INT64:
            nelems = att.get< std::vector< int64_t > >().size();
            break;
        case DT::VEC_UCHAR:
            nelems = att.get< std::vector< unsigned char > >().size();
            break;
        case DT::VEC_UINT16:
            nelems = att.get< std::vector< uint16_t > >().size();
            break;
        case DT::VEC_UINT32:
            nelems = att.get< std::vector< uint32_t > >().size();
            break;
        case DT::VEC_UINT64:
            nelems = att.get< std::vector< uint64_t > >().size();
            break;
        case DT::VEC_FLOAT:
            nelems = att.get< std::vector< float > >().size();
            break;
        case DT::VEC_DOUBLE:
            nelems = att.get< std::vector< double > >().size();
            break;
        case DT::VEC_LONG_DOUBLE:
            nelems = att.get< std::vector< long double > >().size();
            break;
        case DT::VEC_STRING:
            nelems = att.get< std::vector< std::string > >().size();
            break;
        case DT::ARR_DBL_7:
            nelems = 7;
            break;
        case DT::UNDEFINED:
        case DT::DATATYPE:
            throw std::runtime_error("Unknown Attribute datatype");
        default:
            nelems = 1;
    }

    std::unique_ptr< void, std::function< void(void*) > > values = auxiliary::allocatePtr(parameters.dtype, nelems);
    switch( parameters.dtype )
    {
        using DT = Datatype;
        case DT::CHAR:
        {
            auto ptr = reinterpret_cast< char* >(values.get());
            *ptr = att.get< char >();
            break;
        }
        case DT::UCHAR:
        {
            auto ptr = reinterpret_cast< unsigned char* >(values.get());
            *ptr = att.get< unsigned char >();
            break;
        }
        case DT::INT16:
        {
            auto ptr = reinterpret_cast< int16_t* >(values.get());
            *ptr = att.get< int16_t >();
            break;
        }
        case DT::INT32:
        {
            auto ptr = reinterpret_cast< int32_t* >(values.get());
            *ptr = att.get< int32_t >();
            break;
        }
        case DT::INT64:
        {
            auto ptr = reinterpret_cast< int64_t* >(values.get());
            *ptr = att.get< int64_t >();
            break;
        }
        case DT::UINT16:
        {
            auto ptr = reinterpret_cast< uint16_t* >(values.get());
            *ptr = att.get< uint16_t >();
            break;
        }
        case DT::UINT32:
        {
            auto ptr = reinterpret_cast< uint32_t* >(values.get());
            *ptr = att.get< uint32_t >();
            break;
        }
        case DT::UINT64:
        {
            auto ptr = reinterpret_cast< uint64_t* >(values.get());
            *ptr = att.get< uint64_t >();
            break;
        }
        case DT::FLOAT:
        {
            auto ptr = reinterpret_cast< float* >(values.get());
            *ptr = att.get< float >();
            break;
        }
        case DT::DOUBLE:
        {
            auto ptr = reinterpret_cast< double* >(values.get());
            *ptr = att.get< double >();
            break;
        }
        case DT::LONG_DOUBLE:
        {
            auto ptr = reinterpret_cast< long double* >(values.get());
            *ptr = att.get< long double >();
            break;
        }
        case DT::STRING:
        {
            auto const & v = att.get< std::string >();
            values = auxiliary::allocatePtr(Datatype::CHAR, v.length() + 1u);
            strcpy((char*)values.get(), v.c_str());
            break;
        }
        case DT::VEC_CHAR:
        {
            auto ptr = reinterpret_cast< char* >(values.get());
            auto const& vec = att.get< std::vector< char > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_INT16:
        {
            auto ptr = reinterpret_cast< int16_t* >(values.get());
            auto const& vec = att.get< std::vector< int16_t > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_INT32:
        {
            auto ptr = reinterpret_cast< int32_t* >(values.get());
            auto const& vec = att.get< std::vector< int32_t > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_INT64:
        {
            auto ptr = reinterpret_cast< int64_t* >(values.get());
            auto const& vec = att.get< std::vector< int64_t > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_UCHAR:
        {
            auto ptr = reinterpret_cast< unsigned char* >(values.get());
            auto const& vec = att.get< std::vector< unsigned char > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_UINT16:
        {
            auto ptr = reinterpret_cast< uint16_t* >(values.get());
            auto const& vec = att.get< std::vector< uint16_t > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_UINT32:
        {
            auto ptr = reinterpret_cast< uint32_t* >(values.get());
            auto const& vec = att.get< std::vector< uint32_t > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_UINT64:
        {
            auto ptr = reinterpret_cast< uint64_t* >(values.get());
            auto const& vec = att.get< std::vector< uint64_t > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_FLOAT:
        {
            auto ptr = reinterpret_cast< float* >(values.get());
            auto const& vec = att.get< std::vector< float > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_DOUBLE:
        {
            auto ptr = reinterpret_cast< double* >(values.get());
            auto const& vec = att.get< std::vector< double > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_LONG_DOUBLE:
        {
            auto ptr = reinterpret_cast< long double* >(values.get());
            auto const& vec = att.get< std::vector< long double > >();
            for( size_t i = 0; i < vec.size(); ++i )
                ptr[i] = vec[i];
            break;
        }
        case DT::VEC_STRING:
        {
            auto ptr = reinterpret_cast< char** >(values.get());
            auto const& vec = att.get< std::vector< std::string > >();
            for( size_t i = 0; i < vec.size(); ++i )
            {
                size_t size = vec[i].size() + 1;
                ptr[i] = new char[size];
                strncpy(ptr[i], vec[i].c_str(), size);
            }
            break;
        }
        case DT::ARR_DBL_7:
        {
            auto ptr = reinterpret_cast< double* >(values.get());
            auto const& arr = att.get< std::array< double, 7> >();
            for( size_t i = 0; i < 7; ++i )
                ptr[i] = arr[i];
            break;
        }
        case DT::BOOL:
        {
            auto ptr = reinterpret_cast< unsigned char* >(values.get());
            *ptr = static_cast< unsigned char >(att.get< bool >());
            break;
        }
        case DT::UNDEFINED:
        case DT::DATATYPE:
            throw std::runtime_error("Unknown Attribute datatype");
        default:
            throw std::runtime_error("Datatype not implemented in ADIOS IO");
    }

    std::string name = concrete_bp1_file_position(writable);
    if( !auxiliary::ends_with(name, '/') )
        name += '/';
    name += parameters.name;

    int status;
    status = adios_define_attribute_byvalue(m_group,
                                            name.c_str(),
                                            "",
                                            getBP1DataType(parameters.dtype),
                                            nelems,
                                            values.get());
    VERIFY(status == err_no_error, "Internal error: Failed to define ADIOS attribute by value");

    if( parameters.dtype == Datatype::VEC_STRING )
    {
        auto ptr = reinterpret_cast< char** >(values.get());
        for( int i = 0; i < nelems; ++i )
            delete[] ptr[i];
    }
}

void
CommonADIOS1IOHandlerImpl::readDataset(Writable* writable,
                                 Parameter< Operation::READ_DATASET >& parameters)
{
    switch( parameters.dtype )
    {
        using DT = Datatype;
        case DT::DOUBLE:
        case DT::FLOAT:
        case DT::INT16:
        case DT::INT32:
        case DT::INT64:
        case DT::UINT16:
        case DT::UINT32:
        case DT::UINT64:
        case DT::CHAR:
        case DT::UCHAR:
        case DT::BOOL:
            break;
        case DT::UNDEFINED:
            throw std::runtime_error("Unknown Attribute datatype");
        case DT::DATATYPE:
            throw std::runtime_error("Meta-Datatype leaked into IO");
        default:
            throw std::runtime_error("Datatype not implemented in ADIOS1 IO");
    }

    ADIOS_FILE* f;
    f = m_openReadFileHandles.at(m_filePaths.at(writable));

    ADIOS_SELECTION* sel;
    sel = adios_selection_boundingbox(parameters.extent.size(),
                                      parameters.offset.data(),
                                      parameters.extent.data());
    VERIFY(sel != nullptr, "Internal error: Failed to select ADIOS bounding box during dataset reading");
    VERIFY(adios_errno == err_no_error, "Internal error: Failed to select ADIOS bounding box during dataset reading");

    std::string varname = concrete_bp1_file_position(writable);
    void* data = parameters.data.get();

    int status;
    status = adios_schedule_read(f,
                                 sel,
                                 varname.c_str(),
                                 0,
                                 1,
                                 data);
    VERIFY(status == err_no_error, "Internal error: Failed to schedule ADIOS read during dataset reading");
    VERIFY(adios_errno == err_no_error, "Internal error: Failed to schedule ADIOS read during dataset reading");

    m_scheduledReads[f].push_back(sel);
}

void
CommonADIOS1IOHandlerImpl::readAttribute(Writable* writable,
                                   Parameter< Operation::READ_ATT >& parameters)
{
    ADIOS_FILE* f;
    f = m_openReadFileHandles.at(m_filePaths.at(writable));

    std::string attrname = concrete_bp1_file_position(writable);
    if( !auxiliary::ends_with(attrname, '/') )
        attrname += "/";
    attrname += parameters.name;

    ADIOS_DATATYPES datatype = adios_unknown;
    int size = 0;
    void* data = nullptr;

    int status;
    status = adios_get_attr(f,
                            attrname.c_str(),
                            &datatype,
                            &size,
                            &data);
    VERIFY(status == 0, "Internal error: Failed to get ADIOS attribute during attribute read");
    VERIFY(datatype != adios_unknown, "Internal error: Read unknown adios datatype during attribute read");
    VERIFY(size != 0, "Internal error: Read 0-size attribute");

    /* size is returned in number of allocated bytes */
    switch( datatype )
    {
        case adios_byte:
            break;
        case adios_short:
            size /= 2;
            break;
        case adios_integer:
            size /= 4;
            break;
        case adios_long:
            size /= 8;
            break;
        case adios_unsigned_byte:
            break;
        case adios_unsigned_short:
            size /= 2;
            break;
        case adios_unsigned_integer:
            size /= 4;
            break;
        case adios_unsigned_long:
            size /= 8;
            break;
        case adios_real:
            size /= 4;
            break;
        case adios_double:
            size /= 8;
            break;
        case adios_long_double:
            size /= sizeof(long double);
            break;
        case adios_string:
            break;
        case adios_string_array:
            size /= sizeof(char*);
            break;

        case adios_complex:
        case adios_double_complex:
        default:
            throw unsupported_data_error("Unsupported attribute datatype");
    }

    Datatype dtype;
    Attribute a(0);
    if( size == 1 )
    {
        switch( datatype )
        {
            using DT = Datatype;
            case adios_byte:
                dtype = DT::CHAR;
                a = Attribute(*reinterpret_cast< char* >(data));
                break;
            case adios_short:
                dtype = DT::INT16;
                a = Attribute(*reinterpret_cast< int16_t* >(data));
                break;
            case adios_integer:
                dtype = DT::INT32;
                a = Attribute(*reinterpret_cast< int32_t* >(data));
                break;
            case adios_long:
                dtype = DT::INT64;
                a = Attribute(*reinterpret_cast< int64_t* >(data));
                break;
            case adios_unsigned_byte:
                dtype = DT::UCHAR;
                a = Attribute(*reinterpret_cast< unsigned char* >(data));
                break;
            case adios_unsigned_short:
                dtype = DT::UINT16;
                a = Attribute(*reinterpret_cast< uint16_t* >(data));
                break;
            case adios_unsigned_integer:
                dtype = DT::UINT32;
                a = Attribute(*reinterpret_cast< uint32_t* >(data));
                break;
            case adios_unsigned_long:
                dtype = DT::UINT64;
                a = Attribute(*reinterpret_cast< uint64_t* >(data));
                break;
            case adios_real:
                dtype = DT::FLOAT;
                a = Attribute(*reinterpret_cast< float* >(data));
                break;
            case adios_double:
                dtype = DT::DOUBLE;
                a = Attribute(*reinterpret_cast< double* >(data));
                break;
            case adios_long_double:
                dtype = DT::LONG_DOUBLE;
                a = Attribute(*reinterpret_cast< long double* >(data));
                break;
            case adios_string:
            {
                dtype = DT::STRING;
                auto c = reinterpret_cast< char* >(data);
                a = Attribute(auxiliary::strip(std::string(c, std::strlen(c)), {'\0'}));
                break;
            }


            case adios_string_array:
            case adios_complex:
            case adios_double_complex:
            default:
                throw unsupported_data_error("Unsupported attribute datatype");
        }
    }
    else
    {
        switch( datatype )
        {
            using DT = Datatype;
            case adios_byte:
            {
                dtype = DT::VEC_CHAR;
                auto c = reinterpret_cast< char* >(data);
                std::vector< char > vc;
                vc.resize(size);
                for( int i = 0; i < size; ++i )
                    vc[i] = c[i];
                a = Attribute(vc);
                break;
            }
            case adios_short:
            {
                dtype = DT::VEC_INT16;
                auto i16 = reinterpret_cast< int16_t* >(data);
                std::vector< int16_t > vi;
                vi.resize(size);
                for( int i = 0; i < size; ++i )
                    vi[i] = i16[i];
                a = Attribute(vi);
                break;
            }
            case adios_integer:
            {
                dtype = DT::VEC_INT32;
                auto i32 = reinterpret_cast< int32_t* >(data);
                std::vector< int32_t > vi;
                vi.resize(size);
                for( int i = 0; i < size; ++i )
                    vi[i] = i32[i];
                a = Attribute(vi);
                break;
            }
            case adios_long:
            {
                dtype = DT::VEC_INT64;
                auto i64 = reinterpret_cast< int64_t* >(data);
                std::vector< int64_t > vi;
                vi.resize(size);
                for( int i = 0; i < size; ++i )
                    vi[i] = i64[i];
                a = Attribute(vi);
                break;
            }
            case adios_unsigned_byte:
            {
                dtype = DT::VEC_UCHAR;
                auto uc = reinterpret_cast< unsigned char* >(data);
                std::vector< unsigned char > vuc;
                vuc.resize(size);
                for( int i = 0; i < size; ++i )
                    vuc[i] = uc[i];
                a = Attribute(vuc);
                break;
            }
            case adios_unsigned_short:
            {
                dtype = DT::VEC_UINT16;
                auto ui16 = reinterpret_cast< uint16_t* >(data);
                std::vector< uint16_t > vi;
                vi.resize(size);
                for( int i = 0; i < size; ++i )
                    vi[i] = ui16[i];
                a = Attribute(vi);
                break;
            }
            case adios_unsigned_integer:
            {
                dtype = DT::VEC_UINT32;
                auto ui32 = reinterpret_cast< uint32_t* >(data);
                std::vector< uint32_t > vi;
                vi.resize(size);
                for( int i = 0; i < size; ++i )
                    vi[i] = ui32[i];
                a = Attribute(vi);
                break;
            }
            case adios_unsigned_long:
            {
                dtype = DT::VEC_UINT64;
                auto ui64 = reinterpret_cast< uint64_t* >(data);
                std::vector< uint64_t > vi;
                vi.resize(size);
                for( int i = 0; i < size; ++i )
                    vi[i] = ui64[i];
                a = Attribute(vi);
                break;
            }
            case adios_real:
            {
                dtype = DT::VEC_FLOAT;
                auto f4 = reinterpret_cast< float* >(data);
                std::vector< float > vf;
                vf.resize(size);
                for( int i = 0; i < size; ++i )
                    vf[i] = f4[i];
                a = Attribute(vf);
                break;
            }
            case adios_double:
            {
                dtype = DT::VEC_DOUBLE;
                auto d8 = reinterpret_cast< double* >(data);
                std::vector< double > vd;
                vd.resize(size);
                for( int i = 0; i < size; ++i )
                    vd[i] = d8[i];
                a = Attribute(vd);
                break;
            }
            case adios_long_double:
            {
                dtype = DT::VEC_LONG_DOUBLE;
                auto ld = reinterpret_cast< long double* >(data);
                std::vector< long double > vld;
                vld.resize(size);
                for( int i = 0; i < size; ++i )
                    vld[i] = ld[i];
                a = Attribute(vld);
                break;
            }
            case adios_string:
            {
                dtype = DT::STRING;
                a = Attribute(auxiliary::strip(std::string(reinterpret_cast< char* >(data), size), {'\0'}));
                break;
            }
            case adios_string_array:
            {
                dtype = DT::VEC_STRING;
                auto c = reinterpret_cast< char** >(data);
                std::vector< std::string > vs;
                vs.resize(size);
                for( int i = 0; i < size; ++i )
                {
                    vs[i] = auxiliary::strip(std::string(c[i], std::strlen(c[i])), {'\0'});
                    /** @todo pointer should be freed, but this causes memory corruption */
                    //free(c[i]);
                }
                a = Attribute(vs);
                break;
            }

            case adios_complex:
            case adios_double_complex:
            default:
                throw unsupported_data_error("Unsupported attribute datatype");
        }
    }

    free(data);

    *parameters.dtype = dtype;
    *parameters.resource = a.getResource();
}

void
CommonADIOS1IOHandlerImpl::listPaths(Writable* writable,
                               Parameter< Operation::LIST_PATHS >& parameters)
{
    ADIOS_FILE* f;
    f = m_openReadFileHandles.at(m_filePaths.at(writable));

    std::string name = concrete_bp1_file_position(writable);

    std::unordered_set< std::string > paths;
    for( int i = 0; i < f->nvars; ++i )
    {
        char* str = f->var_namelist[i];
        std::string s(str, std::strlen(str));
        if( auxiliary::starts_with(s, name) )
        {
            /* remove the writable's path from the name */
            s = auxiliary::replace_first(s, name, "");
            if( std::any_of(s.begin(), s.end(), [](char c) { return c == '/'; }) )
            {
                /* there are more path levels after the current writable */
                s = s.substr(0, s.find_first_of('/'));
                paths.emplace(s);
            }
        }
    }
    for( int i = 0; i < f->nattrs; ++i )
    {
        char* str = f->attr_namelist[i];
        std::string s(str, std::strlen(str));
        if( auxiliary::starts_with(s, name) )
        {
            /* remove the writable's path from the name */
            s = auxiliary::replace_first(s, name, "");
            /* remove the attribute name */
            s = s.substr(0, s.find_last_of('/'));
            if( std::any_of(s.begin(), s.end(), [](char c) { return c == '/'; }) )
            {
                /* this is an attribute of the writable */
                s = s.substr(0, s.find_first_of('/'));
                paths.emplace(s);
            }
        }
    }

    *parameters.paths = std::vector< std::string >(paths.begin(), paths.end());
}

void
CommonADIOS1IOHandlerImpl::listDatasets(Writable* writable,
                                  Parameter< Operation::LIST_DATASETS >& parameters)
{
    ADIOS_FILE* f;
    f = m_openReadFileHandles.at(m_filePaths.at(writable));

    std::string name = concrete_bp1_file_position(writable);

    std::unordered_set< std::string > paths;
    for( int i = 0; i < f->nvars; ++i )
    {
        char* str = f->var_namelist[i];
        std::string s(str, std::strlen(str));
        if( auxiliary::starts_with(s, name) )
        {
            /* remove the writable's path from the name */
            s = auxiliary::replace_first(s, name, "");
            if( std::none_of(s.begin(), s.end(), [](char c) { return c == '/'; }) )
            {
                /* this is a dataset of the writable */
                paths.emplace(s);
            }
        }
    }

    *parameters.datasets = std::vector< std::string >(paths.begin(), paths.end());
}

void
CommonADIOS1IOHandlerImpl::listAttributes(Writable* writable,
                                    Parameter< Operation::LIST_ATTS >& parameters)
{
    ADIOS_FILE* f;
    f = m_openReadFileHandles.at(m_filePaths.at(writable));

    std::string name = concrete_bp1_file_position(writable);

    if( !auxiliary::ends_with(name, '/') )
    {
        /* writable is a dataset and corresponds to an ADIOS variable */
        ADIOS_VARINFO* info;
        info = adios_inq_var(f,
                             name.c_str());
        VERIFY(adios_errno == err_no_error, "Internal error: Failed to inquire ADIOS variable during attribute listing");
        VERIFY(info != nullptr, "Internal error: Failed to inquire ADIOS variable during attribute listing");

        name += '/';
        parameters.attributes->reserve(info->nattrs);
        for( int i = 0; i < info->nattrs; ++i )
        {
            char* c = f->attr_namelist[info->attr_ids[i]];
            parameters.attributes->push_back(auxiliary::replace_first(std::string(c, std::strlen(c)), name, ""));
        }

        adios_free_varinfo(info);
    } else
    {
        /* there is no ADIOS variable associated with the writable */
        std::unordered_set< std::string > attributes;
        for( int i = 0; i < f->nattrs; ++i )
        {
            char* str = f->attr_namelist[i];
            std::string s(str, std::strlen(str));
            if( auxiliary::starts_with(s, name) )
            {
                /* remove the writable's path from the name */
                s = auxiliary::replace_first(s, name, "");
                if( std::none_of(s.begin(), s.end(), [](char c) { return c == '/'; }) )
                {
                    /* this is an attribute of the writable */
                    attributes.insert(s);
                }
            }
        }
        *parameters.attributes = std::vector< std::string >(attributes.begin(), attributes.end());
    }
}
