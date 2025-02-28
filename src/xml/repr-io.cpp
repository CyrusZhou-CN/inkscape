// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Dirty DOM-like  tree
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *
 * Copyright (C) 1999-2002 Lauris Kaplinski
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstring>
#include <string>
#include <stdexcept>

#include <libxml/parser.h>
#include <libxml/xinclude.h>

#include "xml/repr.h"
#include "xml/attribute-record.h"
#include "xml/rebase-hrefs.h"
#include "xml/simple-document.h"
#include "xml/text-node.h"
#include "xml/node.h"

#include "io/sys.h"
#include "io/stream/stringstream.h"
#include "io/stream/gzipstream.h"
#include "io/stream/uristream.h"

#include "extension/extension.h"

#include "attribute-rel-util.h"
#include "attribute-sort-util.h"

#include "preferences.h"

#include <glibmm/miscutils.h>

using Inkscape::IO::Writer;
using Inkscape::XML::Document;
using Inkscape::XML::SimpleDocument;
using Inkscape::XML::Node;
using Inkscape::XML::AttributeRecord;
using Inkscape::XML::AttributeVector;
using Inkscape::XML::rebase_href_attrs;

Document *sp_repr_do_read (xmlDocPtr doc, const gchar *default_ns);
static Node *sp_repr_svg_read_node (Document *xml_doc, xmlNodePtr node, const gchar *default_ns, std::map<std::string, std::string> &prefix_map);
static gint sp_repr_qualified_name (gchar *p, gint len, xmlNsPtr ns, const xmlChar *name, const gchar *default_ns, std::map<std::string, std::string> &prefix_map);
static void sp_repr_write_stream_root_element(Node *repr, Writer &out,
                                              bool add_whitespace, gchar const *default_ns,
                                              int inlineattrs, int indent,
                                              gchar const *old_href_abs_base,
                                              gchar const *new_href_abs_base);

static void sp_repr_write_stream_element(Node *repr, Writer &out,
                                         gint indent_level, bool add_whitespace,
                                         Glib::QueryQuark elide_prefix,
                                         const AttributeVector & attributes,
                                         int inlineattrs, int indent,
                                         gchar const *old_href_abs_base,
                                         gchar const *new_href_abs_base);


class XmlSource
{
public:
    XmlSource()
        : filename(nullptr),
          encoding(nullptr),
          fp(nullptr),
          firstFewLen(0),
          instr(nullptr),
          gzin(nullptr)
    {
        for (unsigned char & k : firstFew)
        {
            k=0;
        }
    }
    virtual ~XmlSource()
    {
        close();
        if ( encoding ) {
            g_free(encoding);
            encoding = nullptr;
        }
    }

    int setFile( char const * filename );

    xmlDocPtr readXml();

    static int readCb( void * context, char * buffer, int len );
    static int closeCb( void * context );

    char const* getEncoding() const { return encoding; }
    int read( char * buffer, int len );
    int close();
private:
    const char* filename;
    char* encoding;
    FILE* fp;
    unsigned char firstFew[4];
    int firstFewLen;
    Inkscape::IO::FileInputStream* instr;
    Inkscape::IO::GzipInputStream* gzin;
};

int XmlSource::setFile(char const *filename)
{
    int retVal = -1;

    this->filename = filename;

    fp = Inkscape::IO::fopen_utf8name(filename, "r");
    if ( fp ) {
        // First peek in the file to see what it is
        memset( firstFew, 0, sizeof(firstFew) );

        size_t some = fread( firstFew, 1, 4, fp );
        if ( fp ) {
            // first check for compression
            if ( (some >= 2) && (firstFew[0] == 0x1f) && (firstFew[1] == 0x8b) ) {
                //g_message(" the file being read is gzip'd. extract it");
                fclose(fp);
                fp = nullptr;
                fp = Inkscape::IO::fopen_utf8name(filename, "r");
                instr = new Inkscape::IO::FileInputStream(fp);
                gzin = new Inkscape::IO::GzipInputStream(*instr);

                memset( firstFew, 0, sizeof(firstFew) );
                some = 0;
                int single = 0;
                while ( some < 4 && single >= 0 )
                {
                    single = gzin->get();
                    if ( single >= 0 ) {
                        firstFew[some++] = 0x0ff & single;
                    } else {
                        break;
                    }
                }
            }

            int encSkip = 0;
            if ( (some >= 2) &&(firstFew[0] == 0xfe) && (firstFew[1] == 0xff) ) {
                encoding = g_strdup("UTF-16BE");
                encSkip = 2;
            } else if ( (some >= 2) && (firstFew[0] == 0xff) && (firstFew[1] == 0xfe) ) {
                encoding = g_strdup("UTF-16LE");
                encSkip = 2;
            } else if ( (some >= 3) && (firstFew[0] == 0xef) && (firstFew[1] == 0xbb) && (firstFew[2] == 0xbf) ) {
                encoding = g_strdup("UTF-8");
                encSkip = 3;
            }

            if ( encSkip ) {
                memmove( firstFew, firstFew + encSkip, (some - encSkip) );
                some -= encSkip;
            }

            firstFewLen = some;
            retVal = 0; // no error
        }
    }
    return retVal;
}

xmlDocPtr XmlSource::readXml()
{
    int parse_options = XML_PARSE_HUGE | XML_PARSE_RECOVER;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool allowNetAccess = prefs->getBool("/options/externalresources/xml/allow_net_access", false);
    if (!allowNetAccess) parse_options |= XML_PARSE_NONET;

    return xmlReadIO(readCb, closeCb, this, filename, getEncoding(), parse_options);
}

int XmlSource::readCb( void * context, char * buffer, int len )
{
    int retVal = -1;

    if ( context ) {
        XmlSource* self = static_cast<XmlSource*>(context);
        retVal = self->read( buffer, len );
    }
    return retVal;
}

int XmlSource::closeCb(void * context)
{
    if ( context ) {
        XmlSource* self = static_cast<XmlSource*>(context);
        self->close();
    }
    return 0;
}

int XmlSource::read( char *buffer, int len )
{
    int retVal = 0;
    size_t got = 0;

    if ( firstFewLen > 0 ) {
        int some = (len < firstFewLen) ? len : firstFewLen;
        memcpy( buffer, firstFew, some );
        if ( len < firstFewLen ) {
            memmove( firstFew, firstFew + some, (firstFewLen - some) );
        }
        firstFewLen -= some;
        got = some;
    } else if ( gzin ) {
        int single = 0;
        while ( (static_cast<int>(got) < len) && (single >= 0) )
        {
            single = gzin->get();
            if ( single >= 0 ) {
                buffer[got++] = 0x0ff & single;
            } else {
                break;
            }
        }
    } else {
        got = fread( buffer, 1, len, fp );
    }

    if ( feof(fp) ) {
        retVal = got;
    } else if ( ferror(fp) ) {
        retVal = -1;
    } else {
        retVal = got;
    }

    return retVal;
}

int XmlSource::close()
{
    if ( gzin ) {
        gzin->close();
        delete gzin;
        gzin = nullptr;
    }
    if ( instr ) {
        instr->close();
        fp = nullptr;
        delete instr;
        instr = nullptr;
    }
    if ( fp ) {
        fclose(fp);
        fp = nullptr;
    }
    return 0;
}

/**
 * Reads XML from a file, and returns the Document.
 * The default namespace can also be specified, if desired.
 * XIncude is dangerous to support during use-cases like automated file format conversion, so it is off by default.
 *
 * \param filename The actual file to read from. UTF8 encoded.
 *
 * \param default_ns Default namespace for the document, can be nullptr.
 *
 * \param xinclude Process XInclude directives, which is off by default for security.
 */
Document *sp_repr_read_file (const gchar * filename, const gchar *default_ns, bool xinclude)
{
    xmlDocPtr doc = nullptr;
    Document * rdoc = nullptr;

    xmlSubstituteEntitiesDefault(1);

    g_return_val_if_fail(filename != nullptr, NULL);
    if (!Inkscape::IO::file_test(filename, G_FILE_TEST_EXISTS)) {
        g_warning("Can't open file: %s (doesn't exist)", filename);
        return nullptr;
    }
    /* fixme: A file can disappear at any time, including between now and when we actually try to
     * open it.  Get rid of the above test once we're sure that we correctly handle
     * non-existence. */

    // TODO: bulia, please look over
    gsize bytesRead = 0;
    gsize bytesWritten = 0;
    GError* error = nullptr;
    // TODO: need to replace with our own fopen and reading
    gchar* localFilename = g_filename_from_utf8(filename, -1, &bytesRead, &bytesWritten, &error);
    g_return_val_if_fail(localFilename != nullptr, NULL);

    Inkscape::IO::dump_fopen_call(filename, "N");

    XmlSource src;

    if (src.setFile(filename) == 0) {
        doc = src.readXml();
        if (xinclude && doc && doc->properties && xmlXIncludeProcessFlags(doc, XML_PARSE_NOXINCNODE) < 0) {
            g_warning("XInclude processing failed for %s", filename);
        }
        rdoc = sp_repr_do_read(doc, default_ns);
    }

    if (doc) {
        xmlFreeDoc(doc);
    }

    if (localFilename) {
        g_free(localFilename);
    }

    return rdoc;
}

/**
 * Reads and parses XML from a buffer, returning it as an Document
 */
Document *sp_repr_read_mem (const gchar * buffer, gint length, const gchar *default_ns)
{
    xmlDocPtr doc;
    Document * rdoc;

    xmlSubstituteEntitiesDefault(1);

    g_return_val_if_fail (buffer != nullptr, NULL);

    int parser_options = XML_PARSE_HUGE | XML_PARSE_RECOVER;
    parser_options |= XML_PARSE_NONET; // TODO: should we allow network access?
                                       // proper solution would be to check the preference "/options/externalresources/xml/allow_net_access"
                                       // as done in XmlSource::readXml which gets called by the analogous sp_repr_read_file()
                                       // but sp_repr_read_mem() seems to be called in locations where Inkscape::Preferences::get() fails badly
    doc = xmlReadMemory (const_cast<gchar *>(buffer), length, nullptr, nullptr, parser_options);

    rdoc = sp_repr_do_read (doc, default_ns);
    if (doc) {
        xmlFreeDoc (doc);
    }
    return rdoc;
}

/**
 * Reads and parses XML from a buffer, returning it as an Document
 */
Document *sp_repr_read_buf (const Glib::ustring &buf, const gchar *default_ns)
{
    return sp_repr_read_mem(buf.c_str(), buf.size(), default_ns);
}


namespace Inkscape {

struct compare_quark_ids {
    bool operator()(Glib::QueryQuark const &a, Glib::QueryQuark const &b) const {
        return a.id() < b.id();
    }
};

}

namespace {

typedef std::map<Glib::QueryQuark, Glib::QueryQuark, Inkscape::compare_quark_ids> PrefixMap;

Glib::QueryQuark qname_prefix(Glib::QueryQuark qname) {
    static PrefixMap prefix_map;
    PrefixMap::iterator iter = prefix_map.find(qname);
    if ( iter != prefix_map.end() ) {
        return (*iter).second;
    } else {
        gchar const *name_string=g_quark_to_string(qname);
        gchar const *prefix_end=strchr(name_string, ':');
        if (prefix_end) {
            Glib::Quark prefix=Glib::ustring(name_string, prefix_end);
            prefix_map.insert(PrefixMap::value_type(qname, prefix));
            return prefix;
        } else {
            return GQuark(0);
        }
    }
}

}

namespace {

void promote_to_namespace(Node *repr, const gchar *prefix) {
    if ( repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE ) {
        GQuark code = repr->code();
        if (!qname_prefix(code).id()) {
            gchar *svg_name = g_strconcat(prefix, ":", g_quark_to_string(code), nullptr);
            repr->setCodeUnsafe(g_quark_from_string(svg_name));
            g_free(svg_name);
        }
        for ( Node *child = repr->firstChild() ; child ; child = child->next() ) {
            promote_to_namespace(child, prefix);
        }
    }
}

/**
 * When an xml document can be parsed, but it's name spaces are not recognised
 * we can repair the document and force the use of the svg namespace.
 *
 * This can help us make use of some older svg files which use xml ENTITIES
 * which is a feature that should never be allowed to be used for security.
 */
void repair_namespace(Node *repr, const gchar *prefix) {
    if ( repr->type() == Inkscape::XML::NodeType::ELEMENT_NODE ) {
        gchar *svg_name = nullptr;
        if (strncmp(repr->name(), "ns:", 3) == 0) {
            svg_name = g_strconcat(prefix, ":", repr->name() + 3, nullptr);
        } else if (strncmp(repr->name(), "svg0:", 5) == 0) {
            svg_name = g_strconcat(prefix, ":", repr->name() + 5, nullptr);
        }
        if (svg_name) {
            repr->setCodeUnsafe(g_quark_from_string(svg_name));
            g_free(svg_name);
        }
        for ( Node *child = repr->firstChild() ; child ; child = child->next() ) {
            repair_namespace(child, prefix);
        }
    }
}

}

/**
 * Reads in a XML file to create a Document
 */
Document *sp_repr_do_read (xmlDocPtr doc, const gchar *default_ns)
{
    if (doc == nullptr) {
        return nullptr;
    }
    xmlNodePtr node=xmlDocGetRootElement (doc);
    if (node == nullptr) {
        return nullptr;
    }

    std::map<std::string, std::string> prefix_map;

    Document *rdoc = new Inkscape::XML::SimpleDocument();

    Node *root=nullptr;
    for ( node = doc->children ; node != nullptr ; node = node->next ) {
        if (node->type == XML_ELEMENT_NODE) {
            Node *repr=sp_repr_svg_read_node(rdoc, node, default_ns, prefix_map);
            rdoc->appendChild(repr);
            Inkscape::GC::release(repr);

            if (!root) {
                root = repr;
            } else {
                root = nullptr;
                break;
            }
        } else if ( node->type == XML_COMMENT_NODE || node->type == XML_PI_NODE ) {
            Node *repr=sp_repr_svg_read_node(rdoc, node, default_ns, prefix_map);
            rdoc->appendChild(repr);
            Inkscape::GC::release(repr);
        }
    }

    if (root != nullptr) {
        /* promote elements of some XML documents that don't use namespaces
         * into their default namespace */
        if (!strcmp(root->name(), "ns:svg") || !strcmp(root->name(), "svg0:svg")) {
            g_warning("Detected broken namespace \"%s\" in the SVG file, attempting to work around it", root->name());
            repair_namespace(root, "svg");
        } else if ( default_ns && !strchr(root->name(), ':') ) {
            if ( !strcmp(default_ns, SP_SVG_NS_URI) ) {
                promote_to_namespace(root, "svg");
            }
            if ( !strcmp(default_ns, INKSCAPE_EXTENSION_URI) ) {
                promote_to_namespace(root, INKSCAPE_EXTENSION_NS_NC);
            }
        }


        // Clean unnecessary attributes and style properties from SVG documents. (Controlled by
        // preferences.)  Note: internal Inkscape svg files will also be cleaned (filters.svg,
        // icons.svg). How can one tell if a file is internal?
        if ( !strcmp(root->name(), "svg:svg" ) ) {
            Inkscape::Preferences *prefs = Inkscape::Preferences::get();
            bool clean = prefs->getBool("/options/svgoutput/check_on_reading");
            if( clean ) {
                sp_attribute_clean_tree( root );
            }
        }
    }

    return rdoc;
}

gint sp_repr_qualified_name (gchar *p, gint len, xmlNsPtr ns, const xmlChar *name, const gchar */*default_ns*/, std::map<std::string, std::string> &prefix_map)
{
    const xmlChar *prefix;
    if (ns){
        if (ns->href ) {
            prefix = reinterpret_cast<const xmlChar*>( sp_xml_ns_uri_prefix(reinterpret_cast<const gchar*>(ns->href),
                                                                            reinterpret_cast<const char*>(ns->prefix)) );
            prefix_map[reinterpret_cast<const char*>(prefix)] = reinterpret_cast<const char*>(ns->href);
        }
        else {
            prefix = nullptr;
        }
    }
    else {
        prefix = nullptr;
    }

    if (prefix) {
        return g_snprintf (p, len, "%s:%s", reinterpret_cast<const gchar*>(prefix), name);
    } else {
        return g_snprintf (p, len, "%s", name);
    }
}

static Node *sp_repr_svg_read_node (Document *xml_doc, xmlNodePtr node, const gchar *default_ns, std::map<std::string, std::string> &prefix_map)
{
    xmlAttrPtr prop;
    xmlNodePtr child;
    gchar c[256];

    if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE) {

        if (node->content == nullptr || *(node->content) == '\0') {
            return nullptr; // empty text node
        }

        // Since libxml2 2.9.0, only element nodes are checked, thus check parent.
        // Note: this only handles XML's rules for white space. SVG's specific rules
        // are handled in sp-string.cpp.
        bool preserve = (xmlNodeGetSpacePreserve (node->parent) == 1);

        xmlChar *p;
        for (p = node->content; *p && g_ascii_isspace (*p) && !preserve; p++)
            ; // skip all whitespace

        if (!(*p)) { // this is an all-whitespace node, and preserve == default
            return nullptr; // we do not preserve all-whitespace nodes unless we are asked to
        }

        // We keep track of original node type so that CDATA sections are preserved on output.
        return xml_doc->createTextNode(reinterpret_cast<gchar *>(node->content),
                                       node->type == XML_CDATA_SECTION_NODE );
    }

    if (node->type == XML_COMMENT_NODE) {
        return xml_doc->createComment(reinterpret_cast<gchar *>(node->content));
    }

    if (node->type == XML_PI_NODE) {
        return xml_doc->createPI(reinterpret_cast<const gchar *>(node->name),
                                 reinterpret_cast<const gchar *>(node->content));
    }

    if (node->type == XML_ENTITY_DECL) {
        return nullptr;
    }

    sp_repr_qualified_name (c, 256, node->ns, node->name, default_ns, prefix_map);
    Node *repr = xml_doc->createElement(c);
    /* TODO remember node->ns->prefix if node->ns != NULL */

    for (prop = node->properties; prop != nullptr; prop = prop->next) {
        if (prop->children) {
            sp_repr_qualified_name (c, 256, prop->ns, prop->name, default_ns, prefix_map);
            repr->setAttribute(c, reinterpret_cast<gchar*>(prop->children->content));
            /* TODO remember prop->ns->prefix if prop->ns != NULL */
        }
    }

    if (node->content) {
        repr->setContent(reinterpret_cast<gchar*>(node->content));
    }

    for (child = node->xmlChildrenNode; child != nullptr; child = child->next) {
        Node *crepr = sp_repr_svg_read_node (xml_doc, child, default_ns, prefix_map);
        if (crepr) {
            repr->appendChild(crepr);
            Inkscape::GC::release(crepr);
        }
    }

    return repr;
}


static void sp_repr_save_writer(Document *doc, Inkscape::IO::Writer *out,
                    gchar const *default_ns,
                    gchar const *old_href_abs_base,
                    gchar const *new_href_abs_base)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool inlineattrs = prefs->getBool("/options/svgoutput/inlineattrs");
    int indent = prefs->getInt("/options/svgoutput/indent", 2);

    /* fixme: do this The Right Way */
    out->writeString( "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n" );

    const gchar *str = static_cast<Node *>(doc)->attribute("doctype");
    if (str) {
        out->writeString( str );
    }

    for (Node *repr = sp_repr_document_first_child(doc);
         repr; repr = repr->next())
    {
        Inkscape::XML::NodeType const node_type = repr->type();
        if ( node_type == Inkscape::XML::NodeType::ELEMENT_NODE ) {
            sp_repr_write_stream_root_element(repr, *out, TRUE, default_ns, inlineattrs, indent,
                                              old_href_abs_base, new_href_abs_base);
        } else {
            sp_repr_write_stream(repr, *out, 0, TRUE, GQuark(0), inlineattrs, indent,
                                 old_href_abs_base, new_href_abs_base);
            if ( node_type == Inkscape::XML::NodeType::COMMENT_NODE ) {
                out->writeChar('\n');
            }
        }
    }
}


Glib::ustring sp_repr_save_buf(Document *doc)
{   
    Inkscape::IO::StringOutputStream souts;
    Inkscape::IO::OutputStreamWriter outs(souts);

    sp_repr_save_writer(doc, &outs, SP_INKSCAPE_NS_URI, nullptr, nullptr);

    outs.close();
    Glib::ustring buf = souts.getString();

    return buf;
}


void sp_repr_save_stream(Document *doc, FILE *fp, gchar const *default_ns, bool compress,
                    gchar const *const old_href_abs_base,
                    gchar const *const new_href_abs_base)
{
    Inkscape::IO::FileOutputStream bout(fp);
    Inkscape::IO::GzipOutputStream *gout = compress ? new Inkscape::IO::GzipOutputStream(bout) : nullptr;
    Inkscape::IO::OutputStreamWriter *out  = compress ? new Inkscape::IO::OutputStreamWriter( *gout ) : new Inkscape::IO::OutputStreamWriter( bout );

    sp_repr_save_writer(doc, out, default_ns, old_href_abs_base, new_href_abs_base);

    delete out;
    delete gout;
}



/**
 * Returns true if file successfully saved.
 *
 * \param filename The actual file to do I/O to, which might be a temp file.
 *
 * \param for_filename The base URI [actually filename] to assume for purposes of rewriting
 *              xlink:href attributes.
 */
bool sp_repr_save_rebased_file(Document *doc, gchar const *const filename, gchar const *default_ns,
                          gchar const *old_base, gchar const *for_filename)
{
    if (!filename) {
        return false;
    }

    bool compress;
    {
        size_t const filename_len = strlen(filename);
        compress = ( filename_len > 5
                     && strcasecmp(".svgz", filename + filename_len - 5) == 0 );
    }

    Inkscape::IO::dump_fopen_call( filename, "B" );
    FILE *file = Inkscape::IO::fopen_utf8name(filename, "w");
    if (file == nullptr) {
        return false;
    }

    std::string old_href_abs_base;
    std::string new_href_abs_base;

    if (old_base) {
        old_href_abs_base = old_base;
        if (!Glib::path_is_absolute(old_href_abs_base)) {
            old_href_abs_base = Glib::build_filename(Glib::get_current_dir(), old_href_abs_base);
        }
    }

    if (for_filename) {
        if (Glib::path_is_absolute(for_filename)) {
            new_href_abs_base = Glib::path_get_dirname(for_filename);
        } else {
            std::string const cwd = Glib::get_current_dir();
            std::string const for_abs_filename = Glib::build_filename(cwd, for_filename);
            new_href_abs_base = Glib::path_get_dirname(for_abs_filename);
        }

        /* effic: Once we're confident that we never need (or never want) to resort
         * to using sodipodi:absref instead of the xlink:href value,
         * then we should do `if streq() { free them and set both to NULL; }'. */
    }
    sp_repr_save_stream(doc, file, default_ns, compress, old_href_abs_base.c_str(), new_href_abs_base.c_str());

    if (fclose (file) != 0) {
        return false;
    }

    return true;
}

/**
 * Returns true iff file successfully saved.
 */
bool sp_repr_save_file(Document *doc, gchar const *const filename, gchar const *default_ns)
{
    return sp_repr_save_rebased_file(doc, filename, default_ns, nullptr, nullptr);
}


/* (No doubt this function already exists elsewhere.) */
static void repr_quote_write (Writer &out, const gchar * val, bool attr)
{
    if (val) {
        for (; *val != '\0'; val++) {
            switch (*val) {
                case '"': out.writeString( "&quot;" ); break;
                case '&': out.writeString( "&amp;" ); break;
                case '<': out.writeString( "&lt;" ); break;
                case '>': out.writeString( "&gt;" ); break;
                case '\n': out.writeString( attr ? "&#10;" : "\n" ); break;
                default: out.writeChar( *val ); break;
            }
        }
    }
}

static void repr_write_comment( Writer &out, const gchar * val, bool addWhitespace, gint indentLevel, int indent )
{
    if ( indentLevel > 16 ) {
        indentLevel = 16;
    }
    if (addWhitespace && indent) {
        for (gint i = 0; i < indentLevel; i++) {
            for (gint j = 0; j < indent; j++) {
                out.writeChar(' ');
            }
        }
    }

    out.printf("<!--%s-->", val);

    if (addWhitespace) {
        out.writeChar('\n');
    }
}

namespace {

typedef std::map<Glib::QueryQuark, gchar const *, Inkscape::compare_quark_ids> LocalNameMap;
typedef std::map<Glib::QueryQuark, Inkscape::Util::ptr_shared, Inkscape::compare_quark_ids> NSMap;

gchar const *qname_local_name(Glib::QueryQuark qname) {
    static LocalNameMap local_name_map;
    LocalNameMap::iterator iter = local_name_map.find(qname);
    if ( iter != local_name_map.end() ) {
        return (*iter).second;
    } else {
        gchar const *name_string=g_quark_to_string(qname);
        gchar const *prefix_end=strchr(name_string, ':');
        if (prefix_end) {
            return prefix_end + 1;
        } else {
            return name_string;
        }
    }
}

void add_ns_map_entry(NSMap &ns_map, Glib::QueryQuark prefix) {
    using Inkscape::Util::ptr_shared;
    using Inkscape::Util::share_unsafe;

    static const Glib::QueryQuark xml_prefix("xml");

    NSMap::iterator iter=ns_map.find(prefix);
    if ( iter == ns_map.end() ) {
        if (prefix.id()) {
            gchar const *uri=sp_xml_ns_prefix_uri(g_quark_to_string(prefix));
            if (uri) {
                ns_map.insert(NSMap::value_type(prefix, share_unsafe(uri)));
            } else if ( prefix != xml_prefix ) {
                g_warning("No namespace known for normalized prefix %s", g_quark_to_string(prefix));
            }
        } else {
            ns_map.insert(NSMap::value_type(prefix, ptr_shared()));
        }
    }
}

void populate_ns_map(NSMap &ns_map, Node &repr) {
    if ( repr.type() == Inkscape::XML::NodeType::ELEMENT_NODE ) {
        add_ns_map_entry(ns_map, qname_prefix(repr.code()));
        for ( const auto & iter : repr.attributeList() )
        {
            Glib::QueryQuark prefix=qname_prefix(iter.key);
            if (prefix.id()) {
                add_ns_map_entry(ns_map, prefix);
            }
        }
        for ( Node *child=repr.firstChild() ;
              child ; child = child->next() )
        {
            populate_ns_map(ns_map, *child);
        }
    }
}

}

static void sp_repr_write_stream_root_element(Node *repr, Writer &out,
                                  bool add_whitespace, gchar const *default_ns,
                                  int inlineattrs, int indent,
                                  gchar const *const old_href_base,
                                  gchar const *const new_href_base)
{
    using Inkscape::Util::ptr_shared;

    g_assert(repr != nullptr);

    // Clean unnecessary attributes and stype properties. (Controlled by preferences.)
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    bool clean = prefs->getBool("/options/svgoutput/check_on_writing");
    if (clean) sp_attribute_clean_tree( repr );

    // Sort attributes in a canonical order (helps with "diffing" SVG files).only if not set disable optimizations
    bool sort = !prefs->getBool("/options/svgoutput/disable_optimizations") && prefs->getBool("/options/svgoutput/sort_attributes");
    if (sort) sp_attribute_sort_tree( *repr );

    Glib::QueryQuark xml_prefix=g_quark_from_static_string("xml");

    NSMap ns_map;
    populate_ns_map(ns_map, *repr);

    Glib::QueryQuark elide_prefix=GQuark(0);
    if ( default_ns && ns_map.find(GQuark(0)) == ns_map.end() ) {
        elide_prefix = g_quark_from_string(sp_xml_ns_uri_prefix(default_ns, nullptr));
    }

    auto attributes = repr->attributeList(); // copy

    using Inkscape::Util::share_string;
    for (auto iter : ns_map) 
    {
        Glib::QueryQuark prefix=iter.first;
        ptr_shared ns_uri=iter.second;

        if (prefix.id()) {
            if ( prefix != xml_prefix ) {
                if ( elide_prefix == prefix ) {
                    //repr->setAttribute(share_string("xmlns"), share_string(ns_uri));
                    attributes.emplace_back(g_quark_from_static_string("xmlns"), ns_uri);
                }

                Glib::ustring attr_name="xmlns:";
                attr_name.append(g_quark_to_string(prefix));
                GQuark key = g_quark_from_string(attr_name.c_str());
                //repr->setAttribute(share_string(attr_name.c_str()), share_string(ns_uri));
                attributes.emplace_back(key, ns_uri);
            }
        } else {
            // if there are non-namespaced elements, we can't globally
            // use a default namespace
            elide_prefix = GQuark(0);
        }
    }

    return sp_repr_write_stream_element(repr, out, 0, add_whitespace, elide_prefix, attributes,
                                        inlineattrs, indent, old_href_base, new_href_base);
}

void sp_repr_write_stream( Node *repr, Writer &out, gint indent_level,
                           bool add_whitespace, Glib::QueryQuark elide_prefix,
                           int inlineattrs, int indent,
                           gchar const *const old_href_base,
                           gchar const *const new_href_base)
{
    switch (repr->type()) {
        case Inkscape::XML::NodeType::TEXT_NODE: {
            auto textnode = dynamic_cast<const Inkscape::XML::TextNode *>(repr);
            assert(textnode);
            if (textnode->is_CData()) {
                // Preserve CDATA sections, not converting '&' to &amp;, etc.
                out.printf( "<![CDATA[%s]]>", repr->content() );
            } else {
                repr_quote_write( out, repr->content(), false );
            }
            break;
        }
        case Inkscape::XML::NodeType::COMMENT_NODE: {
            repr_write_comment( out, repr->content(), add_whitespace, indent_level, indent );
            break;
        }
        case Inkscape::XML::NodeType::PI_NODE: {
            out.printf( "<?%s %s?>", repr->name(), repr->content() );
            break;
        }
        case Inkscape::XML::NodeType::ELEMENT_NODE: {
            sp_repr_write_stream_element( repr, out, indent_level,
                                          add_whitespace, elide_prefix,
                                          repr->attributeList(),
                                          inlineattrs, indent,
                                          old_href_base, new_href_base);
            break;
        }
        case Inkscape::XML::NodeType::DOCUMENT_NODE: {
            g_assert_not_reached();
            break;
        }
        default: {
            g_assert_not_reached();
        }
    }
}

Glib::ustring sp_repr_write_buf(Node *repr, int indent_level, bool add_whitespace, Glib::QueryQuark elide_prefix,
                                int inlineattrs, int indent, char const *old_href_base,
                                char const *new_href_base)
{
    Inkscape::IO::StringOutputStream souts;
    Inkscape::IO::OutputStreamWriter outs(souts);

    sp_repr_write_stream(repr, outs, indent_level, add_whitespace, elide_prefix, inlineattrs, indent, old_href_base,
                         new_href_base);

    outs.close();
    return souts.getString();
}

void sp_repr_write_stream_element( Node * repr, Writer & out,
                                   gint indent_level, bool add_whitespace,
                                   Glib::QueryQuark elide_prefix,
                                   const AttributeVector & attributes, 
                                   int inlineattrs, int indent,
                                   gchar const *old_href_base,
                                   gchar const *new_href_base )
{
    Node *child = nullptr;
    bool loose = false;
    bool const add_whitespace_parent = add_whitespace;

    g_return_if_fail (repr != nullptr);

    if ( indent_level > 16 ) {
        indent_level = 16;
    }

    if (add_whitespace && indent) {
        for (gint i = 0; i < indent_level; i++) {
            for (gint j = 0; j < indent; j++) {
                out.writeChar(' ');
            }
        }
    }

    GQuark code = repr->code();
    gchar const *element_name;
    if ( elide_prefix == qname_prefix(code) ) {
        element_name = qname_local_name(code);
    } else {
        element_name = g_quark_to_string(code);
    }
    out.printf( "<%s", element_name );

    // If this is a <text> element, suppress formatting whitespace
    // for its content and children:
    if (strcmp(repr->name(), "svg:text")     == 0 ||
        strcmp(repr->name(), "svg:flowRoot") == 0) {
        add_whitespace = false;
    } else {
        // Suppress formatting whitespace for xml:space="preserve"
        gchar const *xml_space_attr = repr->attribute("xml:space");
        if (g_strcmp0(xml_space_attr, "preserve") == 0) {
            add_whitespace = false;
        } else if (g_strcmp0(xml_space_attr, "default") == 0) {
            add_whitespace = true;
        }
    }

    const auto rbd = rebase_href_attrs(old_href_base, new_href_base, attributes);
    for (const auto &iter : rbd) {
        if (!inlineattrs) {
            out.writeChar('\n');
            if (indent) {
                for ( gint i = 0 ; i < indent_level + 1 ; i++ ) {
                    for ( gint j = 0 ; j < indent ; j++ ) {
                        out.writeChar(' ');
                    }
                }
            }
        }
        out.printf(" %s=\"", g_quark_to_string(iter.key));
        repr_quote_write(out, iter.value, true);
        out.writeChar('"');
    }

    loose = TRUE;
    for (child = repr->firstChild() ; child != nullptr; child = child->next()) {
        if (child->type() == Inkscape::XML::NodeType::TEXT_NODE) {
            loose = FALSE;
            break;
        }
    }

    if (repr->firstChild()) {
        out.writeChar('>');
        if (loose && add_whitespace) {
            out.writeChar('\n');
        }
        for (child = repr->firstChild(); child != nullptr; child = child->next()) {
            sp_repr_write_stream(child, out, ( loose ? indent_level + 1 : 0 ),
                                 add_whitespace, elide_prefix, inlineattrs, indent,
                                 old_href_base, new_href_base);
        }

        if (loose && add_whitespace && indent) {
            for (gint i = 0; i < indent_level; i++) {
                for ( gint j = 0 ; j < indent ; j++ ) {
                    out.writeChar(' ');
                }
            }
        }
        out.printf( "</%s>", element_name );
    } else {
        out.writeString( " />" );
    }

    if (add_whitespace_parent) {
        out.writeChar('\n');
    }
}


/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
