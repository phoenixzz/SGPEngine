XmlDocument::XmlDocument (const String& documentText)
    : originalText (documentText),
      input (nullptr),
      ignoreEmptyTextElements (true)
{
}

XmlDocument::XmlDocument (const File& file)
    : input (nullptr),
      ignoreEmptyTextElements (true),
      inputSource (new FileInputSource (file))
{
}

XmlDocument::~XmlDocument()
{
}

XmlElement* XmlDocument::parse (const File& file)
{
    XmlDocument doc (file);
    return doc.getDocumentElement();
}

XmlElement* XmlDocument::parse (const String& xmlData)
{
    XmlDocument doc (xmlData);
    return doc.getDocumentElement();
}

void XmlDocument::setInputSource (InputSource* const newSource) noexcept
{
    inputSource = newSource;
}

void XmlDocument::setEmptyTextElementsIgnored (const bool shouldBeIgnored) noexcept
{
    ignoreEmptyTextElements = shouldBeIgnored;
}

namespace XmlIdentifierChars
{
    static bool isIdentifierCharSlow (const sgp_wchar c) noexcept
    {
        return CharacterFunctions::isLetterOrDigit (c)
                 || c == '_' || c == '-' || c == ':' || c == '.';
    }

    static bool isIdentifierChar (const sgp_wchar c) noexcept
    {
        static const uint32 legalChars[] = { 0, 0x7ff6000, 0x87fffffe, 0x7fffffe, 0 };

        return ((int) c < (int) 5 * 32) ? ((legalChars [c >> 5] & (1 << (c & 31))) != 0)
                                                                      : isIdentifierCharSlow (c);
    }

    /*static void generateIdentifierCharConstants()
    {
        uint32 n[8] = { 0 };
        for (int i = 0; i < 256; ++i)
            if (isIdentifierCharSlow (i))
                n[i >> 5] |= (1 << (i & 31));

        String s;
        for (int i = 0; i < 8; ++i)
            s << "0x" << String::toHexString ((int) n[i]) << ", ";

        DBG (s);
    }*/
}

XmlElement* XmlDocument::getDocumentElement (const bool onlyReadOuterDocumentElement)
{
    String textToParse (originalText);

    if (textToParse.isEmpty() && inputSource != nullptr)
    {
        ScopedPointer <InputStream> in (inputSource->createInputStream());

        if (in != nullptr)
        {
            MemoryOutputStream data;
            data.writeFromInputStream (*in, onlyReadOuterDocumentElement ? 8192 : -1);
            textToParse = data.toString();

            if (! onlyReadOuterDocumentElement)
                originalText = textToParse;
        }
    }

    input = textToParse.getCharPointer();
    lastError = String::empty;
    errorOccurred = false;
    outOfData = false;
    needToLoadDTD = true;

    if (textToParse.isEmpty())
    {
        lastError = "not enough input";
    }
    else
    {
        skipHeader();

        if (input.getAddress() != nullptr)
        {
            ScopedPointer <XmlElement> result (readNextElement (! onlyReadOuterDocumentElement));

            if (! errorOccurred)
                return result.release();
        }
        else
        {
            lastError = "incorrect xml header";
        }
    }

    return nullptr;
}

const String& XmlDocument::getLastParseError() const noexcept
{
    return lastError;
}

void XmlDocument::setLastError (const String& desc, const bool carryOn)
{
    lastError = desc;
    errorOccurred = ! carryOn;
}

String XmlDocument::getFileContents (const String& filename) const
{
    if (inputSource != nullptr)
    {
        const ScopedPointer <InputStream> in (inputSource->createInputStreamFor (filename.trim().unquoted()));

        if (in != nullptr)
            return in->readEntireStreamAsString();
    }

    return String::empty;
}

sgp_wchar XmlDocument::readNextChar() noexcept
{
    const sgp_wchar c = input.getAndAdvance();

    if (c == 0)
    {
        outOfData = true;
        --input;
    }

    return c;
}

int XmlDocument::findNextTokenLength() noexcept
{
    int len = 0;
    sgp_wchar c = *input;

    while (XmlIdentifierChars::isIdentifierChar (c))
        c = input [++len];

    return len;
}

void XmlDocument::skipHeader()
{
    const int headerStart = input.indexOf (CharPointer_UTF8 ("<?xml"));

    if (headerStart >= 0)
    {
        const int headerEnd = (input + headerStart).indexOf (CharPointer_UTF8 ("?>"));
        if (headerEnd < 0)
            return;

       #if SGP_DEBUG
        const String header (input + headerStart, (size_t) (headerEnd - headerStart));
        const String encoding (header.fromFirstOccurrenceOf ("encoding", false, true)
                                     .fromFirstOccurrenceOf ("=", false, false)
                                     .fromFirstOccurrenceOf ("\"", false, false)
                                     .upToFirstOccurrenceOf ("\"", false, false).trim());

        /* If you load an XML document with a non-UTF encoding type, it may have been
           loaded wrongly.. Since all the files are read via the normal sgp file streams,
           they're treated as UTF-8, so by the time it gets to the parser, the encoding will
           have been lost. Best plan is to stick to utf-8 or if you have specific files to
           read, use your own code to convert them to a unicode String, and pass that to the
           XML parser.
        */
        jassert (encoding.isEmpty() || encoding.startsWithIgnoreCase ("utf-"));
       #endif

        input += headerEnd + 2;
    }

    skipNextWhiteSpace();

    const int docTypeIndex = input.indexOf (CharPointer_UTF8 ("<!DOCTYPE"));
    if (docTypeIndex < 0)
        return;

    input += docTypeIndex + 9;
    const String::CharPointerType docType (input);

    int n = 1;

    while (n > 0)
    {
        const sgp_wchar c = readNextChar();

        if (outOfData)
            return;

        if (c == '<')
            ++n;
        else if (c == '>')
            --n;
    }

    dtdText = String (docType, (size_t) (input.getAddress() - (docType.getAddress() + 1))).trim();
}

void XmlDocument::skipNextWhiteSpace()
{
    for (;;)
    {
        sgp_wchar c = *input;

        while (CharacterFunctions::isWhitespace (c))
            c = *++input;

        if (c == 0)
        {
            outOfData = true;
            break;
        }
        else if (c == '<')
        {
            if (input[1] == '!'
                 && input[2] == '-'
                 && input[3] == '-')
            {
                input += 4;
                const int closeComment = input.indexOf (CharPointer_UTF8 ("-->"));

                if (closeComment < 0)
                {
                    outOfData = true;
                    break;
                }

                input += closeComment + 3;
                continue;
            }
            else if (input[1] == '?')
            {
                input += 2;
                const int closeBracket = input.indexOf (CharPointer_UTF8 ("?>"));

                if (closeBracket < 0)
                {
                    outOfData = true;
                    break;
                }

                input += closeBracket + 2;
                continue;
            }
        }

        break;
    }
}

void XmlDocument::readQuotedString (String& result)
{
    const sgp_wchar quote = readNextChar();

    while (! outOfData)
    {
        const sgp_wchar c = readNextChar();

        if (c == quote)
            break;

        --input;

        if (c == '&')
        {
            readEntity (result);
        }
        else
        {
            const String::CharPointerType start (input);
            size_t numChars = 0;

            for (;;)
            {
                const sgp_wchar character = *input;

                if (character == quote)
                {
                    result.appendCharPointer (start, numChars);
                    ++input;
                    return;
                }
                else if (character == '&')
                {
                    result.appendCharPointer (start, numChars);
                    break;
                }
                else if (character == 0)
                {
                    outOfData = true;
                    setLastError ("unmatched quotes", false);
                    break;
                }

                ++input;
                ++numChars;
            }
        }
    }
}

XmlElement* XmlDocument::readNextElement (const bool alsoParseSubElements)
{
    XmlElement* node = nullptr;

    skipNextWhiteSpace();
    if (outOfData)
        return nullptr;

    const int openBracket = input.indexOf ((sgp_wchar) '<');

    if (openBracket >= 0)
    {
        input += openBracket + 1;
        int tagLen = findNextTokenLength();

        if (tagLen == 0)
        {
            // no tag name - but allow for a gap after the '<' before giving an error
            skipNextWhiteSpace();
            tagLen = findNextTokenLength();

            if (tagLen == 0)
            {
                setLastError ("tag name missing", false);
                return node;
            }
        }

        node = new XmlElement (String (input, (size_t) tagLen));
        input += tagLen;
        LinkedListPointer<XmlElement::XmlAttributeNode>::Appender attributeAppender (node->attributes);

        // look for attributes
        for (;;)
        {
            skipNextWhiteSpace();

            const sgp_wchar c = *input;

            // empty tag..
            if (c == '/' && input[1] == '>')
            {
                input += 2;
                break;
            }

            // parse the guts of the element..
            if (c == '>')
            {
                ++input;

                if (alsoParseSubElements)
                    readChildElements (node);

                break;
            }

            // get an attribute..
            if (XmlIdentifierChars::isIdentifierChar (c))
            {
                const int attNameLen = findNextTokenLength();

                if (attNameLen > 0)
                {
                    const String::CharPointerType attNameStart (input);
                    input += attNameLen;

                    skipNextWhiteSpace();

                    if (readNextChar() == '=')
                    {
                        skipNextWhiteSpace();

                        const sgp_wchar nextChar = *input;

                        if (nextChar == '"' || nextChar == '\'')
                        {
                            XmlElement::XmlAttributeNode* const newAtt
                                = new XmlElement::XmlAttributeNode (String (attNameStart, (size_t) attNameLen),
                                                                    String::empty);

                            readQuotedString (newAtt->value);
                            attributeAppender.append (newAtt);
                            continue;
                        }
                    }
                }
            }
            else
            {
                if (! outOfData)
                    setLastError ("illegal character found in " + node->getTagName() + ": '" + c + "'", false);
            }

            break;
        }
    }

    return node;
}

void XmlDocument::readChildElements (XmlElement* parent)
{
    LinkedListPointer<XmlElement>::Appender childAppender (parent->firstChildElement);

    for (;;)
    {
        const String::CharPointerType preWhitespaceInput (input);
        skipNextWhiteSpace();

        if (outOfData)
        {
            setLastError ("unmatched tags", false);
            break;
        }

        if (*input == '<')
        {
            if (input[1] == '/')
            {
                // our close tag..
                const int closeTag = input.indexOf ((sgp_wchar) '>');

                if (closeTag >= 0)
                    input += closeTag + 1;

                break;
            }
            else if (input[1] == '!'
                  && input[2] == '['
                  && input[3] == 'C'
                  && input[4] == 'D'
                  && input[5] == 'A'
                  && input[6] == 'T'
                  && input[7] == 'A'
                  && input[8] == '[')
            {
                input += 9;
                const String::CharPointerType inputStart (input);

                size_t len = 0;

                for (;;)
                {
                    if (*input == 0)
                    {
                        setLastError ("unterminated CDATA section", false);
                        outOfData = true;
                        break;
                    }
                    else if (input[0] == ']'
                              && input[1] == ']'
                              && input[2] == '>')
                    {
                        input += 3;
                        break;
                    }

                    ++input;
                    ++len;
                }

                childAppender.append (XmlElement::createTextElement (String (inputStart, len)));
            }
            else
            {
                // this is some other element, so parse and add it..
                if (XmlElement* const n = readNextElement (true))
                    childAppender.append (n);
                else
                    break;
            }
        }
        else  // must be a character block
        {
            input = preWhitespaceInput; // roll back to include the leading whitespace
            String textElementContent;

            for (;;)
            {
                const sgp_wchar c = *input;

                if (c == '<')
                    break;

                if (c == 0)
                {
                    setLastError ("unmatched tags", false);
                    outOfData = true;
                    return;
                }

                if (c == '&')
                {
                    String entity;
                    readEntity (entity);

                    if (entity.startsWithChar ('<') && entity [1] != 0)
                    {
                        const String::CharPointerType oldInput (input);
                        const bool oldOutOfData = outOfData;

                        input = entity.getCharPointer();
                        outOfData = false;

                        for (;;)
                        {
                            XmlElement* const n = readNextElement (true);

                            if (n == nullptr)
                                break;

                            childAppender.append (n);
                        }

                        input = oldInput;
                        outOfData = oldOutOfData;
                    }
                    else
                    {
                        textElementContent += entity;
                    }
                }
                else
                {
                    const String::CharPointerType start (input);
                    size_t len = 0;

                    for (;;)
                    {
                        const sgp_wchar nextChar = *input;

                        if (nextChar == '<' || nextChar == '&')
                        {
                            break;
                        }
                        else if (nextChar == 0)
                        {
                            setLastError ("unmatched tags", false);
                            outOfData = true;
                            return;
                        }

                        ++input;
                        ++len;
                    }

                    textElementContent.appendCharPointer (start, len);
                }
            }

            if ((! ignoreEmptyTextElements) || textElementContent.containsNonWhitespaceChars())
            {
                childAppender.append (XmlElement::createTextElement (textElementContent));
            }
        }
    }
}

void XmlDocument::readEntity (String& result)
{
    // skip over the ampersand
    ++input;

    if (input.compareIgnoreCaseUpTo (CharPointer_UTF8 ("amp;"), 4) == 0)
    {
        input += 4;
        result += '&';
    }
    else if (input.compareIgnoreCaseUpTo (CharPointer_UTF8 ("quot;"), 5) == 0)
    {
        input += 5;
        result += '"';
    }
    else if (input.compareIgnoreCaseUpTo (CharPointer_UTF8 ("apos;"), 5) == 0)
    {
        input += 5;
        result += '\'';
    }
    else if (input.compareIgnoreCaseUpTo (CharPointer_UTF8 ("lt;"), 3) == 0)
    {
        input += 3;
        result += '<';
    }
    else if (input.compareIgnoreCaseUpTo (CharPointer_UTF8 ("gt;"), 3) == 0)
    {
        input += 3;
        result += '>';
    }
    else if (*input == '#')
    {
        int charCode = 0;
        ++input;

        if (*input == 'x' || *input == 'X')
        {
            ++input;
            int numChars = 0;

            while (input[0] != ';')
            {
                const int hexValue = CharacterFunctions::getHexDigitValue (input[0]);

                if (hexValue < 0 || ++numChars > 8)
                {
                    setLastError ("illegal escape sequence", true);
                    break;
                }

                charCode = (charCode << 4) | hexValue;
                ++input;
            }

            ++input;
        }
        else if (input[0] >= '0' && input[0] <= '9')
        {
            int numChars = 0;

            while (input[0] != ';')
            {
                if (++numChars > 12)
                {
                    setLastError ("illegal escape sequence", true);
                    break;
                }

                charCode = charCode * 10 + ((int) input[0] - '0');
                ++input;
            }

            ++input;
        }
        else
        {
            setLastError ("illegal escape sequence", true);
            result += '&';
            return;
        }

        result << (sgp_wchar) charCode;
    }
    else
    {
        const String::CharPointerType entityNameStart (input);
        const int closingSemiColon = input.indexOf ((sgp_wchar) ';');

        if (closingSemiColon < 0)
        {
            outOfData = true;
            result += '&';
        }
        else
        {
            input += closingSemiColon + 1;

            result += expandExternalEntity (String (entityNameStart, (size_t) closingSemiColon));
        }
    }
}

String XmlDocument::expandEntity (const String& ent)
{
    if (ent.equalsIgnoreCase ("amp"))   return String::charToString ('&');
    if (ent.equalsIgnoreCase ("quot"))  return String::charToString ('"');
    if (ent.equalsIgnoreCase ("apos"))  return String::charToString ('\'');
    if (ent.equalsIgnoreCase ("lt"))    return String::charToString ('<');
    if (ent.equalsIgnoreCase ("gt"))    return String::charToString ('>');

    if (ent[0] == '#')
    {
        const sgp_wchar char1 = ent[1];

        if (char1 == 'x' || char1 == 'X')
            return String::charToString (static_cast <sgp_wchar> (ent.substring (2).getHexValue32()));

        if (char1 >= '0' && char1 <= '9')
            return String::charToString (static_cast <sgp_wchar> (ent.substring (1).getIntValue()));

        setLastError ("illegal escape sequence", false);
        return String::charToString ('&');
    }

    return expandExternalEntity (ent);
}

String XmlDocument::expandExternalEntity (const String& entity)
{
    if (needToLoadDTD)
    {
        if (dtdText.isNotEmpty())
        {
            dtdText = dtdText.trimCharactersAtEnd (">");
            tokenisedDTD.addTokens (dtdText, true);

            if (tokenisedDTD [tokenisedDTD.size() - 2].equalsIgnoreCase ("system")
                 && tokenisedDTD [tokenisedDTD.size() - 1].isQuotedString())
            {
                const String fn (tokenisedDTD [tokenisedDTD.size() - 1]);

                tokenisedDTD.clear();
                tokenisedDTD.addTokens (getFileContents (fn), true);
            }
            else
            {
                tokenisedDTD.clear();
                const int openBracket = dtdText.indexOfChar ('[');

                if (openBracket > 0)
                {
                    const int closeBracket = dtdText.lastIndexOfChar (']');

                    if (closeBracket > openBracket)
                        tokenisedDTD.addTokens (dtdText.substring (openBracket + 1,
                                                                   closeBracket), true);
                }
            }

            for (int i = tokenisedDTD.size(); --i >= 0;)
            {
                if (tokenisedDTD[i].startsWithChar ('%')
                     && tokenisedDTD[i].endsWithChar (';'))
                {
                    const String parsed (getParameterEntity (tokenisedDTD[i].substring (1, tokenisedDTD[i].length() - 1)));
                    StringArray newToks;
                    newToks.addTokens (parsed, true);

                    tokenisedDTD.remove (i);

                    for (int j = newToks.size(); --j >= 0;)
                        tokenisedDTD.insert (i, newToks[j]);
                }
            }
        }

        needToLoadDTD = false;
    }

    for (int i = 0; i < tokenisedDTD.size(); ++i)
    {
        if (tokenisedDTD[i] == entity)
        {
            if (tokenisedDTD[i - 1].equalsIgnoreCase ("<!entity"))
            {
                String ent (tokenisedDTD [i + 1].trimCharactersAtEnd (">").trim().unquoted());

                // check for sub-entities..
                int ampersand = ent.indexOfChar ('&');

                while (ampersand >= 0)
                {
                    const int semiColon = ent.indexOf (i + 1, ";");

                    if (semiColon < 0)
                    {
                        setLastError ("entity without terminating semi-colon", false);
                        break;
                    }

                    const String resolved (expandEntity (ent.substring (i + 1, semiColon)));

                    ent = ent.substring (0, ampersand)
                           + resolved
                           + ent.substring (semiColon + 1);

                    ampersand = ent.indexOfChar (semiColon + 1, '&');
                }

                return ent;
            }
        }
    }

    setLastError ("unknown entity", true);

    return entity;
}

String XmlDocument::getParameterEntity (const String& entity)
{
    for (int i = 0; i < tokenisedDTD.size(); ++i)
    {
        if (tokenisedDTD[i] == entity
             && tokenisedDTD [i - 1] == "%"
             && tokenisedDTD [i - 2].equalsIgnoreCase ("<!entity"))
        {
            const String ent (tokenisedDTD [i + 1].trimCharactersAtEnd (">"));

            if (ent.equalsIgnoreCase ("system"))
                return getFileContents (tokenisedDTD [i + 2].trimCharactersAtEnd (">"));

            return ent.trim().unquoted();
        }
    }

    return entity;
}
