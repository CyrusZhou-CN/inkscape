/**
 * Phoebe DOM Implementation.
 *
 * This is a C++ approximation of the W3C DOM model, which follows
 * fairly closely the specifications in the various .idl files, copies of
 * which are provided for reference.  Most important is this one:
 *
 * http://www.w3.org/TR/2004/REC-DOM-Level-3-Core-20040407/idl-definitions.html
 *
 * Authors:
 *   Bob Jamison
 *
 * Copyright (C) 2006 Bob Jamison
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include "charclass.h"
#include "xpathparser.h"


namespace org
{
namespace w3c
{
namespace dom
{
namespace xpath
{


//#########################################################################
//# M E S S A G E S
//#########################################################################



void XPathParser::trace(const char *fmt, ...)
{
    if (!debug)
        return;

    FILE *f = stdout;

    va_list args;
    va_start(args, fmt);
    fprintf(f, "XPathParser: ");
    vfprintf(f, fmt, args);
    fprintf(f, "\n");
    va_end(args);
}



void XPathParser::error(const char *fmt, ...)
{
    FILE *f = stdout;
    va_list args;
    va_start(args, fmt);
    fprintf(f, "XPathParser ERROR: ");
    vfprintf(f, fmt, args);
    fprintf(f, "\n");
    va_end(args);

    //Print location in string
    fprintf(f, "%s\n", parsebuf);
    for (int i=0 ; i<position ; i++)
        fprintf(f, " ");
    fprintf(f, "^\n");
}



void XPathParser::traceStack(const char *name, int pos, int depth)
{
    if (!debug)
        return;
    return;
    int indent = depth;

    for (int i=0 ; i<indent ; i++)
        fprintf(stdout, " ");
    fprintf(stdout, "%d %d %s\n", pos, depth, name);

}


//#########################################################################
//# L E X I C A L    S C A N N I N G
//#########################################################################

void XPathParser::lexTokAdd(int type, int loc)
{
    LexTok tok(type, loc);
    lexicalTokens.push_back(tok);
}

void XPathParser::lexTokAdd(int type, int loc, const DOMString &val)
{
    LexTok tok(type, loc, val);
    lexicalTokens.push_back(tok);
}

void XPathParser::lexTokAdd(int type, int loc, double val)
{
    LexTok tok(type, loc, val);
    lexicalTokens.push_back(tok);
}

void XPathParser::lexTokAdd(int type, int loc, long   val)
{
    LexTok tok(type, loc, val);
    lexicalTokens.push_back(tok);
}

void XPathParser::lexicalTokenDump()
{
    printf("####### LEXICAL TOKENS #######\n");
    for (unsigned int i=0 ; i<lexicalTokens.size() ; i++)
        {
        printf("%d : ", i);
        lexicalTokens[i].print();
        }
    printf("##### END LEXICAL TOKENS #####\n\n");
}



LexTok XPathParser::lexTok(int p)
{
    if (p < 0 || p>=(int)lexicalTokens.size())
        {
	LexTok tok;
        return tok;
        }
    return lexicalTokens[p];
}

int XPathParser::lexTokType(int p)
{
    if (p < 0 || p>=(int)lexicalTokens.size())
        return -1;
    return lexicalTokens[p].getType();
}








int XPathParser::peek(int p)
{
    if (p >= parselen)
        return -1;
    position = p;
    return parsebuf[p] ;
}


int XPathParser::get(int p)
{
    if (p >= parselen)
        return -1;
    position = p;
    return parsebuf[p];
}

int XPathParser::skipwhite(int p0)
{
    int p = p0;

    while (p < parselen)
        {
        int ch = peek(p);
        if (!isWhitespace(ch))
            break;
        ch = get(p++);
        }
    return p;
}

int XPathParser::getword(int p0, DOMString &str)
{
    int p = p0;
    while (p < parselen)
        {
        int ch = peek(p);
        if (!isLetterOrDigit(ch))
            break;
        ch = get(p++);
        str.push_back((XMLCh)ch);
        }
    return p;
}

int XPathParser::match(int p, const char *str)
{
    while (*str)
        {
        if (p >= parselen)
            return -1;
        if (parsebuf[p] != *str)
            return -1;
        p++; str++;
        }
    return p;
}




int XPathParser::getNumber(int p0, double &dresult)
{
    int p = p0;
    if (p >= parselen)
        return p0;/*need at least x*/

    bool isdouble = false;
    bool negative = false;

    int ch = parsebuf[p];
    if (ch=='-')
        {
        p++;
        negative = true;
        if (p >= parselen) return p0;
        }

    bool seen_dot    = false;
    bool seen_e      = false;
    bool seen_eminus = false;

    DOMString num;

    int i = p;
    while (i < parselen)
        {
        ch = parsebuf[i];
        if (ch=='.')
            {
            if (seen_dot)
                return p0;
            seen_dot = true;
            isdouble = true;
            }
        else if (ch=='e' || ch=='E')
            {
            if (seen_e || !seen_dot)
                return p0;
            seen_e = true;
            }
        else if (ch=='-' && seen_e)
            {
            if (seen_eminus || !seen_dot)
                return p0;
            seen_eminus = true;
            }
        else if (!isDigit(ch))
            break;
        num.push_back((XMLCh)ch);
        i++;
        }

    if (i == p)/*no digits*/
        return p0;
    if (isdouble)
        {
        const char *begin = num.c_str();
        char *end;
        dresult = strtod(begin,&end);
        if (!end)/*not a number?*/
            {
            error("Error formatting double: %s\n", num.c_str());
            return p0;
            }
        }
    else
        {
        const char *begin = num.c_str();
        char *end;
        dresult = (double)strtol(begin,&end,10);
        if (!end)/*not a number?*/
            {
            error("Error formatting integer: %s\n", num.c_str());
            return p0;
            }
        }
    p = i;
    return p;
}



int XPathParser::getLiteral(int p0, DOMString &result)
{
    int p = p0;
    int ch = peek(p);
    int quotechar = 0;
    if (ch == '"' || ch == '\'')
        {
        quotechar = ch;
        }
    else
        return p0;
    p++;
    while (true)
        {
        if (p >= parselen)
            {
            error("Unterminated literal string");
            return -1;
            }
        ch = peek(p);
        if (ch == quotechar)
            break;
        result.push_back((XMLCh)ch);
        p++;
        }
    p++; //skip over closing "
    return p;
}



/**
 * NCName is a 'non-colonized' name
 */
int XPathParser::getNCName(int p0, DOMString &result)
{
    int p = p0;
    int ch = peek(p);
    if (ch != '_' && !isLetter(ch))
        return p0;

    result.push_back((XMLCh)ch);
    p++;
    while (p < parselen)
        {
        ch = peek(p);
        if (   isLetterOrDigit(ch) ||
               isCombiningChar(ch) ||
               isExtender(ch)      ||
               ch == '.' || ch == '-' || ch == '_' )
           {
           result.push_back((XMLCh)ch);
           p++;
           }
       else
           break;
       }
    return p;
}



/**
 * Name parsing with post-parsing
 */
int XPathParser::getNameTest(int p0, DOMString &result)
{
    int p = p0;
    int ch = peek(p);
    if (ch == '*')
        {
        result.push_back((XMLCh)ch);
        p++;
        return p;
        }

    DOMString ncName;
    int p2 = getNCName(p, ncName);
    if (p2 <= p)
        return p0;

    result = ncName;
    p = p2;

    ch = peek(p);
    if (ch != ':' )//short name. we are done
        {
        return p;
        }

     if (peek(p+1) == ':')  //was  name::  which is ok
        return p;

    result.push_back(':');

    p++;
    ch = peek(p);
    if (ch == '*')
        {
        result.push_back((XMLCh)ch);
        p++;
        return p;
        }

    DOMString ncName2;
    p2 = getNCName(p, ncName2);
    if (p2 <= p)
        {
        if (peek(p) == ':')  //was  name::  which is ok
            return p0;
        error("Nothing after ':' in QName");
        return -1;
        }

    result.append(ncName2);

    p = p2;

    return p;
}



int XPathParser::lexicalScan()
{
    lexicalTokens.clear();

    int p  = 0;
    int p2 = p;

    while (p < parselen)
        {
        p2 = skipwhite(p);
        p = p2;

        //trace("nextChar:%c", peek(p));
        bool selected = false;

        //### LITERAL EXPR TOKENS
        for (int i=2 ; i<=10 ; i++)
            {
            p2 = match(p, exprTokenTable[i].sval);
            if (p2 > p)
                {
                lexTokAdd(exprTokenTable[i].ival, p);
                p = p2;
                selected = true;
                break;
                }
            }
        if (selected)
            continue;

        //### OPERATORS
        for (LookupEntry *entry = operatorTable; entry->sval ; entry++)
            {
            p2 = match(p, entry->sval);
            if (p2 > p)
                {
                long op = (long)entry->ival;
                //according to the disambiguating rule for * in the spec
                if (op == MULTIPLY && lexicalTokens.size() > 0)
                    {
                    int ltyp = lexTokType(lexicalTokens.size()-1);
                    if (ltyp != AMPR   && ltyp != DOUBLE_COLON &&
                        ltyp != LPAREN && ltyp != RBRACKET     &&
                        ltyp != COMMA  && ltyp != OPERATOR        )
                        {
                        lexTokAdd(OPERATOR, p, (long)entry->ival);
                        p = p2;
                        selected = true;
                        break;
                        }
                    }
                else
                    {
                    lexTokAdd(OPERATOR, p, (long)entry->ival);
                    p = p2;
                    selected = true;
                    break;
                    }
                }
            }
        if (selected)
            continue;

        //### NODE TYPES
        for (LookupEntry *entry = nodeTypeTable; entry->sval ; entry++)
            {
            p2 = match(p, entry->sval);
            if (p2 > p)
                {
                lexTokAdd(NODE_TYPE, p, (long)entry->ival);
                p = p2;
                selected = true;
                break;
                }
            }
        if (selected)
            continue;

        //### AXIS NAMES
        for (LookupEntry *entry = axisNameTable; entry->sval ; entry++)
            {
            p2 = match(p, entry->sval);
            if (p2 > p)
                {
                lexTokAdd(AXIS_NAME, p, (long)entry->ival);
                p = p2;
                selected = true;
                break;
                }
            }
        if (selected)
            continue;

        //### NAME TEST
        DOMString ntResult;
        p2 = getNameTest(p, ntResult);
        if (p2 > p)
            {
            int p3 = skipwhite(p2);
            if (peek(p3) == '(')
                lexTokAdd(FUNCTION_NAME, p, ntResult);
            else
                lexTokAdd(NAME_TEST, p, ntResult);
            p = p2;
            selected = true;
            }
        if (selected)
            continue;

        //### VARIABLE REFERENCE
        if (peek(p) == '$')
            {
            p++;
            DOMString qnResult;
            p2 = getNCName(p, qnResult);
            if (p2 > p)
                {
                lexTokAdd(VARIABLE_REFERENCE, p, qnResult);
                p = p2;
                selected = true;
                }
            else
                {
                error("Variable referenced with '$' requires a qualified name\n");
                return -1;
                }
            }
        if (selected)
            continue;

        //### NUMBER
        double numval;
        p2 = getNumber(p, numval);
        if (p2 > p)
            {
            lexTokAdd(NUMBER, p, numval);
            p = p2;
            selected = true;
            }
        if (selected)
            continue;

        //### LITERAL
        DOMString strval;
        p2 = getLiteral(p, strval);
        if (p2 > p)
            {
            lexTokAdd(LITERAL, p, strval);
            p = p2;
            selected = true;
            }
        if (selected)
            continue;

        //### CHAR  (default, none of the above)
        lexTokAdd(CHAR, p, (long) peek(p));
        p++;

        }//while p


    return p;
}






















//#########################################################################
//# X P A T H    G R A M M A R    P A R S I N G
//#########################################################################


void XPathParser::tokAdd(Token *tok)
{
    tokens.add(tok);
}

/**
 * [1]  LocationPath ::=
 *        RelativeLocationPath
 *        | AbsoluteLocationPath
 */
int XPathParser::getLocationPath(int p0, int depth)
{
    traceStack("getLocationPath", p0, depth);
    int p = p0;

    p = skipwhite(p);

    int p2 = getAbsoluteLocationPath(p, depth+1);
    if (p2 > p)
        {
        tokens.add(new TokAbsolute());
        return p2;
        }

    p2 = getRelativeLocationPath(p, depth+1);
    if (p2 > p)
        {
        tokens.add(new TokRelative());
        return p2;
        }

    return p0;
}


/**
 * [2]	AbsoluteLocationPath ::=
 *        '/' RelativeLocationPath?
 *         | AbbreviatedAbsoluteLocationPath
 */
int XPathParser::getAbsoluteLocationPath(int p0, int depth)
{
    traceStack("getAbsoluteLocationPath", p0, depth);

    int p = p0;
    LexTok t = lexTok(p);
    if (t.getType() == OPERATOR && t.getIntValue()==SLASH)
        {
        p++;
        int p2 = getRelativeLocationPath(p, depth+1);
        if (p2 <= p)
            {
            error("Relative path after '/'");
            return -1;
            }
        p = p2;
        return p;
        }

    //AbbreviatedAbsoluteLocationPath
    if (t.getType() == OPERATOR && t.getIntValue()==DOUBLE_SLASH)
        {
        p++;
        int p2 = getRelativeLocationPath(p, depth+1);
        if (p2 <= p)
            {
            error("Relative path after '//'");
            return -1;
            }
        p = p2;
        return p;
        }


    return p0;
}


/**
 * [3] RelativeLocationPath ::=
 *   	 Step
 *       | RelativeLocationPath '/' Step
 *       | AbbreviatedRelativeLocationPath
 */
int XPathParser::getRelativeLocationPath(int p0, int depth)
{
    traceStack("getRelativeLocationPath", p0, depth);
    int p = p0;
    int p2 = getStep(p, depth+1);
    if (p2 < 0)
        return -1;
    if (p2 > p)
        {
        p = p2;
        LexTok t = lexTok(p);
        if (t.getType() == OPERATOR && t.getIntValue()==SLASH)
            {
            p++;
            p2 = getRelativeLocationPath(p, depth+1);
            if (p2 < 0)
                {
                error("Relative path after '/'");
                return -1;
                }
            p = p2;
            return p;
            }
        //AbbreviatedRelativeLocationPath
        if (t.getType() == OPERATOR && t.getIntValue()==DOUBLE_SLASH)
            {
            p++;
            p2 = getRelativeLocationPath(p, depth+1);
            if (p2 < 0)
                {
                error("Relative path after '//'");
                return -1;
                }
            p = p2;
            return p;
            }
        return p;
        }


    return p0;
}


/**
 * [4] Step ::=
 *       AxisSpecifier NodeTest Predicate*
 *       | AbbreviatedStep
 */
int XPathParser::getStep(int p0, int depth)
{
    traceStack("getStep", p0, depth);

    int p = p0;

    lexTok(p).print();

    //This can be (and usually is) 0-length
    int p2 = getAxisSpecifier(p, depth+1);
    if (p2 < 0)
        {
        error("Axis specifier in step section");
        return -1;
        }
    p = p2;
    p2 = getNodeTest(p, depth+1);
    if (p2 < 0)
        {
        error("Node test in step section");
        return -1;
        }

    if (p2 > p)
        {
        p = p2;
        p2 = getPredicate(p, depth+1);
        if (p2 < 0)
            {
            error("Predicate in step section");
            return -1;
            }
        p = p2;
        return p;
        }

    //AbbreviatedStep
    if (lexTokType(p) == DOT)
        {
        p++;
        return p;
        }

    //AbbreviatedStep
    if (lexTokType(p) == DOUBLE_DOT)
        {
        p++;
        return p;
        }

    return p0;
}


/**
 * [5] AxisSpecifier ::=
 *         AxisName '::'
 *         | AbbreviatedAxisSpecifier
 */
int XPathParser::getAxisSpecifier(int p0, int depth)
{
    traceStack("getAxisSpecifier", p0, depth);
    int p = p0;
    if (lexTokType(p) == AXIS_NAME)
        {
        LexTok t = lexTok(p);
        int axisType = t.getIntValue();
        p++;
        if (lexTokType(p) != DOUBLE_COLON)
            {
            error("'::' required after axis name literal");
            return -1;
            }
        p++;
        switch (axisType)
            {
            case ANCESTOR_OR_SELF:
                tokAdd(new TokAxisAncestorOrSelf());
            case ANCESTOR:
                tokAdd(new TokAxisAncestor());
            case ATTRIBUTE:
                tokAdd(new TokAxisAttribute());
            case CHILD:
                tokAdd(new TokAxisChild());
            case DESCENDANT_OR_SELF:
                tokAdd(new TokAxisDescendantOrSelf());
            case DESCENDANT:
                tokAdd(new TokAxisDescendant());
            case FOLLOWING_SIBLING:
                tokAdd(new TokAxisFollowingSibling());
            case FOLLOWING:
                tokAdd(new TokAxisFollowing());
            case NAMESPACE:
                tokAdd(new TokAxisNamespace());
            case PARENT:
                tokAdd(new TokAxisParent());
            case PRECEDING_SIBLING:
                tokAdd(new TokAxisPrecedingSibling());
            case PRECEDING:
                tokAdd(new TokAxisPreceding());
            case SELF:
                tokAdd(new TokAxisSelf());
            default:
                {
                error("unknown axis type %d", axisType);
                return -1;
                }
            }
        return p;
        }

    //AbbreviatedAxisSpecifier
    if (lexTokType(p) == AMPR)
        {
        p++;
        return p;
        }

    return p0;
}


/**
 * [6]  AxisName ::=
 *         'ancestor'
 *         | 'ancestor-or-self'
 *         | 'attribute'
 *         | 'child'
 *         | 'descendant'
 *         | 'descendant-or-self'
 *         | 'following'
 *         | 'following-sibling'
 *         | 'namespace'
 *         | 'parent'
 *         | 'preceding'
 *         | 'preceding-sibling'
 *         | 'self'
 * NOTE: This definition, and those at the bottom, is not
 *   needed.  Its functionality is handled by lexical scanning.
 *   It is left here for reference.
 */
int XPathParser::getAxisName(int p0, int depth)
{
    traceStack("getAxisName", p0, depth);
    return p0;
}


/**
 * [7] NodeTest ::=
 *       NameTest
 *       | NodeType '(' ')'
 *       | 'processing-instruction' '(' Literal ')'
 */
int XPathParser::getNodeTest(int p0, int depth)
{
    traceStack("getNodeTest", p0, depth);
    int p = p0;

    LexTok t = lexTok(p);
    if (t.getType() == NAME_TEST)
        {
        p++;
        tokAdd(new TokNameTest(t.getStringValue()));
        return p;
        }
    if (t.getType() == NODE_TYPE)
        {
        if (t.getIntValue() == PROCESSING_INSTRUCTION)
            {
            if (lexTokType(p)   != LPAREN   ||
                lexTokType(p+1) != LITERAL  ||
                lexTokType(p+2) != RPAREN   )
                {
                error("processing instruction requires (\"literal string\")");
                return -1;
                }
            p += 3;
            }
        else
            {
            if (lexTokType(p+1) != LPAREN ||
                lexTokType(p+2) != RPAREN )
                {
                error("processing instruction requires ()");
                return -1;
                }
            p += 2;
            }
        return p;
        }

    return p0;
}


/**
 * [8]  Predicate ::=
 *         '[' PredicateExpr ']'
 */
int XPathParser::getPredicate(int p0, int depth)
{
    traceStack("getPredicate", p0, depth);

    int p = p0;
    if (lexTokType(p) != LBRACKET)
        return p0;

    p++;
    int p2 = getPredicateExpr(p, depth+1);
    if (p2 <= p)
        {
        error("Predicate expression in predicate");
        return -1;
        }

    p = p2;
    lexTok(p).print();
    if (lexTokType(p) != RBRACKET)
        {
        error("Predicate expression requires closing ']'");
        return -1;
        }
    p++;
    return p;
}


/**
 * [9]  PredicateExpr ::=
 *         Expr
 */
int XPathParser::getPredicateExpr(int p0, int depth)
{
    traceStack("getPredicateExpr", p0, depth);
    int p = p0;
    int p2 = getExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Expression in predicate expression");
        return -1;
        }
    p = p2;
    return p;
}


/**
 * [10] AbbreviatedAbsoluteLocationPath ::=
 *        '//' RelativeLocationPath
 * NOTE: not used. handled in getAbsoluteLocationPath()
 */
int XPathParser::getAbbreviatedAbsoluteLocationPath(int p0, int depth)
{
    traceStack("getAbbreviatedAbsoluteLocationPath", p0, depth);

     return p0;
}

/**
 * [11] AbbreviatedRelativeLocationPath ::=
 *         RelativeLocationPath '//' Step
 * NOTE: not used. handled in getRelativeLocationPath()
 */
int XPathParser::getAbbreviatedRelativeLocationPath(int p0, int depth)
{
    traceStack("getAbbreviatedRelativeLocationPath", p0, depth);
    return p0;
}

/**
 * [12]  AbbreviatedStep ::=
 *           '.'
 *           | '..'
 * NOTE: not used. handled in getStep()
 */
int XPathParser::getAbbreviatedStep(int p0, int depth)
{
    traceStack("getAbbreviatedStep", p0, depth);
    return p0;
}


/**
 * [13] AbbreviatedAxisSpecifier ::=
 *        '@'?
 * NOTE: not used. handled in getAxisSpecifier()
 */
int XPathParser::getAbbreviatedAxisSpecifier(int p0, int depth)
{
    traceStack("getAbbreviatedAxisSpecifier", p0, depth);
    return p0;
}


/**
 * [14] Expr ::=
 *         OrExpr
 */
int XPathParser::getExpr(int p0, int depth)
{
    traceStack("getExpr", p0, depth);

    int p = p0;

    int p2 = getOrExpr(p, depth+1);
    if (p2 < 0)
        {
        error("OR expression in expression");
        return -1;
        }
    p = p2;

    return p;
}


/**
 * [15]  PrimaryExpr ::=
 *          VariableReference
 *          | '(' Expr ')'
 *          | Literal
 *          | Number
 *          | FunctionCall
 */
int XPathParser::getPrimaryExpr(int p0, int depth)
{
    traceStack("getPrimaryExpr", p0, depth);
    int p = p0;
    int p2 = p;

    if (lexTokType(p) == VARIABLE_REFERENCE)
        {
        p++;
        return p;
        }

    if (lexTokType(p) == LPAREN)
        {
        p++;
        p2 = getExpr(p, depth+1);
        if (p2 <= p)
            {
            error("Expression in primary expression");
            return -1;
            }
        p += p2;
        if (lexTokType(p) != RPAREN)
            {
            error("Primary expression requires closing ')'");
            return -1;
            }
        }

    if (lexTokType(p) == LITERAL)
        {
        tokens.add(new TokStr(lexTok(p).getStringValue()));
        p++;
        return p;
        }

    if (lexTokType(p) == NUMBER)
        {
        tokens.add(new TokFloat(lexTok(p).getDoubleValue()));
        p++;
        return p;
        }

    p2 = getFunctionCall(p, depth+1);
    if (p2 < 0)
        {
        error("Function call in primary expression");
        return -1;
        }
    if (p2 > p)
        {
        p = p2;
        return p;
        }

    return p0;
}


/**
 * [16] FunctionCall ::=
 *         FunctionName '(' ( Argument ( ',' Argument )* )? ')'
 */
int XPathParser::getFunctionCall(int p0, int depth)
{
    traceStack("getFunctionCall", p0, depth);
    int p = p0;

    if (lexTokType(p) != FUNCTION_NAME)
        return p0;

    DOMString name = lexTok(p).getStringValue();

    p++;

    if (lexTokType(p) != LPAREN) //this makes a function
        return p0;
    p++;

    int argCount = 0;

    int p2 = getArgument(p, depth+1);
    if (p2 < 0)
        {
        error("Error in function argument");
        return -1;
        }
    if (p2 > p)
        {
        argCount++;
        p = p2;
        while (lexTokType(p) == COMMA)
            {
            p++;
            p2 = getArgument(p, depth+1);
            if (p2 <= p)
                {
                error("Error in function argument");
                return -1;
                }
            if (p2 > p)
                argCount++;
            //do we add a token here?  i dont think so
            p = p2;
            }
        }

    if (lexTokType(p) != RPAREN) //mandatory
        {
        error("Function requires closing ')'");
        return -1;
        }
    p++;

    // Function names from http://www.w3.org/TR/xpath#NT-FunctionName
    if (name == "last")
        tokens.add(new TokFuncLast());
    else if (name == "position")
        tokens.add(new TokFuncPosition());
    else if (name == "count")
        tokens.add(new TokFuncCount());
    else if (name == "id")
        tokens.add(new TokFuncId());
    else if (name == "local-name")
        tokens.add(new TokFuncLocalName());
    else if (name == "namespace-uri")
        tokens.add(new TokFuncNamespaceUri());
    else if (name == "name")
        tokens.add(new TokFuncName());
    else if (name == "string")
        tokens.add(new TokFuncString());
    else if (name == "concat")
        tokens.add(new TokFuncConcat());
    else if (name == "starts-with")
        tokens.add(new TokFuncStartsWith());
    else if (name == "contains")
        tokens.add(new TokFuncContains());
    else if (name == "substring-before")
        tokens.add(new TokFuncSubstringBefore());
    else if (name == "substring-after")
        tokens.add(new TokFuncSubstringAfter());
    else if (name == "substring")
        tokens.add(new TokFuncSubstring());
    else if (name == "string-length")
        tokens.add(new TokFuncStringLength());
    else if (name == "normalize-space")
        tokens.add(new TokFuncNormalizeSpace());
    else if (name == "translate")
        tokens.add(new TokFuncTranslate());
    else if (name == "boolean")
        tokens.add(new TokFuncBoolean());
    else if (name == "not")
        tokens.add(new TokFuncNot());
    else if (name == "true")
        tokens.add(new TokFuncTrue());
    else if (name == "false")
        tokens.add(new TokFuncFalse());
    else if (name == "lang")
        tokens.add(new TokFuncLang());
    else if (name == "number")
        tokens.add(new TokFuncNumber());
    else if (name == "sum")
        tokens.add(new TokFuncSum());
    else if (name == "floor")
        tokens.add(new TokFuncFloor());
    else if (name == "ceiling")
        tokens.add(new TokFuncCeiling());
    else if (name == "round")
        tokens.add(new TokFuncRound());
    else
        {
        error("unknown function name:'%s'", name.c_str());
        return -1;
        }
    return p;
}


/**
 * [17] Argument ::=
 *         Expr
 */
int XPathParser::getArgument(int p0, int depth)
{
    traceStack("getArgument", p0, depth);
    int p = p0;
    int p2 = getExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Argument expression");
        return -1;
        }
    p = p2;
    return p;
}


/**
 * [18]  UnionExpr ::=
 *           PathExpr
 *           | UnionExpr '|' PathExpr
 */
int XPathParser::getUnionExpr(int p0, int depth)
{
    traceStack("getUnionExpr", p0, depth);
    int p = p0;
    int p2 = getPathExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Path expression for union");
        return -1;
        }
    p = p2;
    LexTok t = lexTok(p);
    if (t.getType() == OPERATOR && t.getIntValue() == PIPE)
        {
        p++;
        p2 = getUnionExpr(p, depth+1);
        if (p2 < 0)
            {
            error("OR (|) requires union expression on the left");
            return -1;
            }
        tokens.add(new TokUnion());
        p = p2;
        }
    return p;
}


/**
 * [19]  PathExpr ::=
 *          LocationPath
 *          | FilterExpr
 *          | FilterExpr '/' RelativeLocationPath
 *          | FilterExpr '//' RelativeLocationPath
 */
int XPathParser::getPathExpr(int p0, int depth)
{
    traceStack("getPathExpr", p0, depth);
    int p = p0;
    int p2;

    p2 = getLocationPath(p, depth+1);
    if (p2 < 0)
        {
        error("Location path in path expression");
        return -1;
        }
    if (p2 > p)
        {
        p = p2;
        return p;
        }

    p2 = getFilterExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Filter expression in path expression");
        return -1;
        }
    if (p2 <= p)
        return p0;
    p = p2;

    LexTok t = lexTok(p);
    if (t.getType() == OPERATOR && t.getIntValue() == SLASH)
        {
        p++;
        p2 = getRelativeLocationPath(p, depth+1);
        if (p2 < 0)
            {
            error("Relative location after / in path expression");
            return -1;
            }
        p = p2;
        return p;
        }

    if (t.getType() == OPERATOR && t.getIntValue() == DOUBLE_SLASH)
        {
        p++;
        p2 = getRelativeLocationPath(p, depth+1);
        if (p2 < 0)
            {
            error("Relative location after // in path expression");
            return -1;
            }
        p = p2;
        return p;
        }
    return p;
}


/**
 * [20] FilterExpr ::=
 *         PrimaryExpr
 *         | FilterExpr Predicate
 */
int XPathParser::getFilterExpr(int p0, int depth)
{
    traceStack("getFilterExpr", p0, depth);
    int p = p0;

    int p2 = getPrimaryExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Primary expression in path expression");
        return -1;
        }
    if (p2 > p)
        {
	p = p2;
        while (true)
            {
            p2 = getPredicate(p, depth+1);
            if (p2 < 0)
                {
                error("Predicate in primary expression");
                return -1;
                }
	    if (p2 > p)
		{
		p = p2;
		}
	    else
		break;
           }
        return p;
        }

    return p0;
}


/**
 * [21]  OrExpr ::=
 *           AndExpr
 *           | OrExpr 'or' AndExpr
 */
int XPathParser::getOrExpr(int p0, int depth)
{
    traceStack("getOrExpr", p0, depth);
    int p = p0;
    int p2 = getAndExpr(p, depth+1);
    if (p2 < 0)
        {
        error("AND expression in OR expression");
        return -1;
        }
    if (p2 > p)
        {
        p = p2;
        LexTok t = lexTok(p);
        if (t.getType() == OPERATOR && t.getIntValue() == OR)
            {
            p++;
            p2 = getAndExpr(p, depth+1);
            if (p2 <= p)
                {
                error("AND expression in OR expression");
                return -1;
                }
            p = p2;
            return p;
            }
        tokens.add(new TokOr());
        return p;
        }

    return p0;
}


/**
 * [22]	AndExpr ::=
 *         EqualityExpr
 *         | AndExpr 'and' EqualityExpr
 */
int XPathParser::getAndExpr(int p0, int depth)
{
    traceStack("getAndExpr", p0, depth);
    int p = p0;
    int p2 = getEqualityExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Equality expression in AND expression");
        return -1;
        }
    if (p2 > p)
        {
        p = p2;
        LexTok t = lexTok(p);
        if (t.getType() == OPERATOR && t.getIntValue() == AND)
            {
            p++;
            p2 = getAndExpr(p, depth+1);
            if (p2 <= p)
                {
                error("AND expression after 'and'");
                return -1;
                }
            p = p2;
            return p;
            }
        tokens.add(new TokAnd());
        return p;
        }

    return p0;
}


/**
 * [23]  EqualityExpr ::=
 *           RelationalExpr
 *           | EqualityExpr '=' RelationalExpr
 *           | EqualityExpr '!=' RelationalExpr
 */
int XPathParser::getEqualityExpr(int p0, int depth)
{
    traceStack("getEqualityExpr", p0, depth);
    int p = p0;
    int p2 = getRelationalExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Relation expression in equality expression");
        return -1;
        }
    if (p2 > p)
        {
        p = p2;
        LexTok t = lexTok(p);
        if (t.getType() == OPERATOR && t.getIntValue() == EQUALS)
            {
            p++;
            p2 = getEqualityExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Equality expression expected after ==");
                return -1;
                }
            tokens.add(new TokEquals());
            p = p2;
            return p;
            }

        if (t.getType() == OPERATOR && t.getIntValue() == NOT_EQUALS)
            {
            p++;
            p2 = getEqualityExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Equality expression expected after !=");
                return -1;
                }
            tokens.add(new TokNotEquals());
            p = p2;
            return p;
            }

        return p;
        }

    return p0;
}


/**
 * [24] RelationalExpr ::=
 *         AdditiveExpr
 *         | RelationalExpr '<' AdditiveExpr
 *         | RelationalExpr '>' AdditiveExpr
 *         | RelationalExpr '<=' AdditiveExpr
 *         | RelationalExpr '>=' AdditiveExpr
 */
int XPathParser::getRelationalExpr(int p0, int depth)
{
    traceStack("getRelationalExpr", p0, depth);
    int p = p0;
    int p2 = getAdditiveExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Additive expression in relational expression");
        return -1;
        }
    if (p2 > p)
        {
        p = p2;
        LexTok t = lexTok(p);

        if (t.getType() == OPERATOR && t.getIntValue() == GREATER_THAN)
            {
            p++;
            p2 = getRelationalExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Relational expression after '>'");
                return -1;
                }
            tokens.add(new TokGreaterThan());
            p = p2;
            return p;
            }
        if (t.getType() == OPERATOR && t.getIntValue() == LESS_THAN)
            {
            p++;
            p2 = getRelationalExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Relational expression after '<'");
                return -1;
                }
            tokens.add(new TokLessThan());
            p = p2;
            return p;
            }
        if (t.getType() == OPERATOR && t.getIntValue() == GREATER_THAN_EQUALS)
            {
            p++;
            p2 = getRelationalExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Relational expression after '>='");
                return -1;
                }
            tokens.add(new TokGreaterThanEquals());
            p = p2;
            return p;
            }
        if (t.getType() == OPERATOR && t.getIntValue() == LESS_THAN_EQUALS)
            {
            p++;
            p2 = getRelationalExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Relational expression after '<='");
                return -1;
                }
            tokens.add(new TokLessThanEquals());
            p = p2;
            return p;
            }


        return p;
        }

    return p0;
}


/**
 * [25]  AdditiveExp ::=
 *           MultiplicativeExpr
 *           | AdditiveExpr '+' MultiplicativeExpr
 *           | AdditiveExpr '-' MultiplicativeExpr
 */
int XPathParser::getAdditiveExpr(int p0, int depth)
{
    traceStack("getAdditiveExpr", p0, depth);
    int p = p0;
    int p2 = getMultiplicativeExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Multiplicative expression in additive expression");
        return -1;
        }
    if (p2 > p)
        {
        p = p2;
        LexTok t = lexTok(p);

        if (t.getType() == OPERATOR && t.getIntValue() == PLUS)
            {
            p++;
            p2 = getAdditiveExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Additive expression after '+'");
                return -1;
                }
            tokens.add(new TokPlus());
            p = p2;
            return p;
            }
        if (t.getType() == OPERATOR && t.getIntValue() == MINUS)
            {
            p++;
            p2 = getAdditiveExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Additive expression after '-'");
                return -1;
                }
            tokens.add(new TokMinus());
            p = p2;
            return p;
            }


        return p;
        }

    return p0;
}


/**
 * [26]  MultiplicativeExpr ::=
 *          UnaryExpr
 *          | MultiplicativeExpr MultiplyOperator UnaryExpr
 *          | MultiplicativeExpr 'div' UnaryExpr
 *          | MultiplicativeExpr 'mod' UnaryExpr
 */
int XPathParser::getMultiplicativeExpr(int p0, int depth)
{
    traceStack("getMultiplicativeExpr", p0, depth);
    int p = p0;
    int p2 = getUnaryExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Unary expression in multiplicative expression");
        return -1;
        }
    if (p2 > p)
        {
        p = p2;
        LexTok t = lexTok(p);

        if (t.getType() == OPERATOR && t.getIntValue() == MULTIPLY)
            {
            p++;
            p2 = getMultiplicativeExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Multiplicative expression after '*'");
                return -1;
                }
            tokens.add(new TokMul());
            p = p2;
            return p;
            }

        if (t.getType() == OPERATOR && t.getIntValue() == DIV)
            {
            p++;
            p2 = getMultiplicativeExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Multiplicative expression after 'div'");
                return -1;
                }
            tokens.add(new TokDiv());
            p = p2;
            return p;
            }

        if (t.getType() == OPERATOR && t.getIntValue() == MOD)
            {
            p++;
            p2 = getMultiplicativeExpr(p, depth+1);
            if (p2 <= p)
                {
                error("Multiplicative expression after 'mod'");
                return -1;
                }
            tokens.add(new TokMod());
            p = p2;
            return p;
            }


        return p;
        }

    return p0;
}


/**
 * [27]  UnaryExpr ::=
 *          UnionExpr
 *          | '-' UnaryExpr
 */
int XPathParser::getUnaryExpr(int p0, int depth)
{
    traceStack("getUnaryExpr", p0, depth);
    int p = p0;
    int p2 = getUnionExpr(p, depth+1);
    if (p2 < 0)
        {
        error("Union expression in unary expression");
        return -1;
        }
    if (p2 > p)
        {
        p = p2;
        return p;
        }

    if (lexTokType(p) == '-')
        {
        p++;
        p2 = getUnaryExpr(p, depth+1);
        if (p2 < 0)
            {
            error("Unary expression after '-'");
            return -1;
            }
        tokens.add(new TokNeg());
        p = p2;
        return p;
        }

    return p0;
}


//######################################################
//# NOT USED!!!
//## The grammar definitions below are
//## handled by lexical parsing, and will not be used
//######################################################

/**
 * [28] ExprToken ::=
 *         '(' | ')' | '[' | ']' | '.' | '..' | '@' | ',' | '::'
 *         | NameTest
 *         | NodeType
 *         | Operator
 *         | FunctionName
 *         | AxisName
 *         | Literal
 *         | Number
 *         | VariableReference
 */
int XPathParser::getExprToken(int p0, int depth)
{
    traceStack("getExprToken", p0, depth);
    return p0;
}


/**
 * [29]  Literal ::=
 *           '"' [^"]* '"'
 *           | "'" [^']* "'"
 */
int XPathParser::getLiteral(int p0, int depth)
{
    traceStack("getLiteral", p0, depth);
    return p0;
}


/**
 * [30] Number ::=
 *        Digits ('.' Digits?)?
 *        | '.' Digits
 */
int XPathParser::getNumber(int p0, int depth)
{
    traceStack("getNumber", p0, depth);
    return p0;
}


/**
 * [31] Digits ::=
 *         [0-9]+
 */
int XPathParser::getDigits(int p0, int depth)
{
    traceStack("getDigits", p0, depth);
    return p0;
}


/**
 * [32] Operator ::=
 *         OperatorName
 *         | MultiplyOperator
 *         | '/' | '//' | '|' | '+' | '-' | '='
 *         | '!=' | '<' | '<=' | '>' | '>='
 */
int XPathParser::getOperator(int p0, int depth)
{
    traceStack("getOperator", p0, depth);
    return p0;
}


/**
 * [33]  OperatorName ::=
 *          'and' | 'or' | 'mod' | 'div'
 */
int XPathParser::getOperatorName(int p0, int depth)
{
    traceStack("getOperatorName", p0, depth);
    return p0;
}


/**
 * [34] MultiplyOperator ::=
 *          '*'
 */
int XPathParser::getMultiplyOperator(int p0, int depth)
{
    traceStack("getMultiplyOperator", p0, depth);
    return p0;
}


/**
 * [35] FunctionName ::=
 *          QName - NodeType
 */
int XPathParser::getFunctionName(int p0, int depth)
{
    traceStack("getFunctionName", p0, depth);
    return p0;
}


/**
 * [36] VariableReference ::=
 *          '$' QName
 */
int XPathParser::getVariableReference(int p0, int depth)
{
    traceStack("getVariableReference", p0, depth);
    return p0;
}


/**
 * [37] NameTest ::=
 *         '*'
 *         | NCName ':' '*'
 *         | QName
 */
int XPathParser::getNameTest(int p0, int depth)
{
    traceStack("getNameTest", p0, depth);
    return p0;
}


/**
 * [38] NodeType ::=
 *         'comment'
 *         | 'text'
 *         | 'processing-instruction'
 *         | 'node'
 */
int XPathParser::getNodeType(int p0, int depth)
{
    traceStack("getNodeType", p0, depth);
    return p0;
}


/**
 * [39] ExprWhitespace ::=
 *           S
 */
int XPathParser::getExprWhitespace(int p0, int depth)
{
    traceStack("getExprWhitespace", p0, depth);
    return p0;
}





//#########################################################################
//# H I G H    L E V E L    P A R S I N G
//#########################################################################

/**
 * Parse a candidate XPath string.  Leave a copy in 'tokens.'
 */
bool XPathParser::parse(const DOMString &xpathString)
{
    int p0 = 0;

    DOMString str = xpathString;

    parsebuf = (char *)str.c_str();
    parselen = (int)   str.size();
    position = 0;

    trace("## parsing string: '%s'", parsebuf);

    lexicalScan();
    lexicalTokenDump();

    tokens.clear();//Get ready to store new tokens

    int p = getLocationPath(p0, 0);

    parsebuf = NULL;
    parselen = 0;

    if (p <= p0)
        {
        //return false;
        }

    return true;
}





//#########################################################################
//# E V A L U A T E
//#########################################################################




/**
 * This wraps the two-step call to parse(), then execute() to get a NodeList
 * of matching DOM nodes
 */
NodeList XPathParser::evaluate(const Node *root,
                               const DOMString &xpathString)
{
    NodeList list;

    //### Maybe do caching for speed here

    //### Parse and execute
    //### Error message can be generated as a side effect
    if (!parse(xpathString))
        return list;

    if (debug)
        tokens.dump();

    //### Execute the token list
    list = tokens.execute(root);

    return list;
}



} // namespace xpath
} // namespace dom
} // namespace w3c
} // namespace org
//#########################################################################
//# E N D    O F    F I L E
//#########################################################################




