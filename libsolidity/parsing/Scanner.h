/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.

	This file is derived from the file "scanner.h", which was part of the
	V8 project. The original copyright header follows:

	Copyright 2006-2012, the V8 project authors. All rights reserved.
	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are
	met:

	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above
	  copyright notice, this list of conditions and the following
	  disclaimer in the documentation and/or other materials provided
	  with the distribution.
	* Neither the name of Google Inc. nor the names of its
	  contributors may be used to endorse or promote products derived
	  from this software without specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
	A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
	OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
	LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
	DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
	THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/**
 * @author Christian <c@ethdev.com>
 * @date 2014
 * Solidity scanner.
 */

#pragma once

#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>
#include <libevmasm/SourceLocation.h>
#include <libsolidity/parsing/Token.h>

namespace dev
{
namespace solidity
{


class AstRawString;
class AstValueFactory;
class ParserRecorder;

class CharStream
{
public:
	CharStream(): m_position(0) {}
	explicit CharStream(std::string const& _source): m_source(_source), m_position(0) {}
	int position() const { return m_position; }
	bool isPastEndOfInput(size_t _charsForward = 0) const { return (m_position + _charsForward) >= m_source.size(); }
	char get(size_t _charsForward = 0) const { return m_source[m_position + _charsForward]; }
	char advanceAndGet(size_t _chars = 1);
	char rollback(size_t _amount);

	// DEBUG //
	void insertSource(std::string s){
		m_source.insert(m_position, s);
	}

	// DEBUG //
	void setPosition(int _amount){
		m_position = _amount;
	}
	///////////

	void reset() { m_position = 0; }

	std::string const& source() const { return m_source; }

	///@{
	///@name Error printing helper functions
	/// Functions that help pretty-printing parse errors
	/// Do only use in error cases, they are quite expensive.
	std::string lineAtPosition(int _position) const;
	std::tuple<int, int> translatePositionToLineColumn(int _position) const;
	///@}

private:
	std::string m_source;
	size_t m_position;
};



class Scanner
{
	friend class LiteralScope;
public:

	explicit Scanner(CharStream const& _source = CharStream(), std::string const& _sourceName = "") { reset(_source, _sourceName); }

	// DEBUG //
	void addString(std::string s){
		for(unsigned int i = 0; i < s.length(); i++){
			m_currentToken.literal.push_back(s.at(i));
		}
	}

	void addChar(char c){
		m_currentToken.literal.push_back(c);
	}

	void addSource(std::string s){
		m_source.insertSource(s);
	}

	void updateNextToken(){
		scanToken();
	}

	void addSourceAndScan(std::string s){
		addSource(s);
		scanToken();
	}

	bool getChangeVariable(){
		return changeVariable;
	}

	void setChangeVariable(bool b){
		changeVariable = b;
	}

	void rollBackToken(int offset){
		// offset 만큼 더 rollback 된다
		rollback(sourcePos() - currentLocation().start + offset);
		std::cout << "sourcePos() - currentLocation().start = " << sourcePos() - currentLocation().start << std::endl;
		std::cout << "sourcePos() - currentLocation().start + offset = " << sourcePos() - currentLocation().start + offset << std::endl;
	}

	std::vector<std::string> getTokenLiteralByLocation(SourceLocation location){
		int originalPos;
		int originalLocationStart;
		std::vector<std::string> resultLiteralList;

		// * 알아야 할 것 : position과 location은 다르다. location은 좌표처럼 주어지고 position은 int값임..
		// location 엔 (start, end) 가 있다. start랑 end는 int값임! 즉 어떤 토큰의 위치를 나타내는거고
		// position은 지금 scanner의 가리키는 곳을 뜻하는거겠지

		// std::cout << "== getTokenByLocation function ==" << std::endl;

		originalPos = sourcePos(); // sourcePos() == m_source.position()
		originalLocationStart = currentLocation().start;

		/*
		// check
		std::cout << "originalPos = " << originalPos << std::endl;
		std::cout << "currentLocation().start = " << currentLocation().start << std::endl;
		currentToken();	// current : semicolon
		std::cout << "Next Token: " << peekLiteral() << " (" << std::string(Token::name(peekNextToken())) << ")" << std::endl;
		*/

		// 1. start로 rollback 해서 "end까지"" 토큰 스캔하고 저장
		rollback(originalPos - location.start);
		scanToken();
		while(sourcePos() <= location.end) {
			next();
			resultLiteralList.push_back(currentLiteral());
		}

		/*
		// check
		std::cout << "## ROLLBACK complete!!!" << std::endl;
		std::cout << "Next Token: " << peekLiteral() << " (" << std::string(Token::name(peekNextToken())) << ")" << std::endl;
		*/

		// 2. 그리고 다시 advance로 원래위치로 돌려놓기
		m_source.setPosition(originalLocationStart - 1); // -1 : semicolon length
		scanToken(); // current : semicolon
		next();

		/*
		// check
		std::cout << "## Return to original source code!!!" << std::endl;
		currentToken();
		std::cout << "Next Token: " << peekLiteral() << " (" << std::string(Token::name(peekNextToken())) << ")" << std::endl;

		std::cout << "sourcePos = " << sourcePos() << std::endl;
		std::cout << "currentLocation().start = " << currentLocation().start << std::endl;
		*/

		// 3. 저장한 토큰 String을 리턴
		// std::cout << "== result token : " << resultTokenLiteral << " (" << std::string(Token::name(resultToken)) << ")==" << std::endl;
		return resultLiteralList;
	}

	//////////

	std::string source() const { return m_source.source(); }

	/// Resets the scanner as if newly constructed with _source and _sourceName as input.
	void reset(CharStream const& _source, std::string const& _sourceName);
	/// Resets scanner to the start of input.
	void reset();

	/// @returns the next token and advances input
	Token::Value next();

	///@{
	///@name Information about the current token

	/// @returns the current token
	Token::Value currentToken() const
	{
		//DEBUG//
		std::cout << "Current Token: " << m_currentToken.literal << " (" << std::string(Token::name(m_currentToken.token)) << ")" << std::endl;
		/////////
		return m_currentToken.token;
	}
	ElementaryTypeNameToken currentElementaryTypeNameToken() const
	{
		unsigned firstSize;
		unsigned secondSize;
		std::tie(firstSize, secondSize) = m_currentToken.extendedTokenInfo;
		return ElementaryTypeNameToken(m_currentToken.token, firstSize, secondSize);
	}

	SourceLocation currentLocation() const { return m_currentToken.location; }
	std::string const& currentLiteral() const { return m_currentToken.literal; }
	std::tuple<unsigned, unsigned> const& currentTokenInfo() const { return m_currentToken.extendedTokenInfo; }
	///@}

	///@{
	///@name Information about the current comment token

	SourceLocation currentCommentLocation() const { return m_skippedComment.location; }
	std::string const& currentCommentLiteral() const {
	 return m_skippedComment.literal;
	}
	/// Called by the parser during FunctionDefinition parsing to clear the current comment
	void clearCurrentCommentLiteral() { m_skippedComment.literal.clear(); }

	///@}

	///@{
	///@name Information about the next token

	/// @returns the next token without advancing input.
	Token::Value peekNextToken() const { return m_nextToken.token; }
	SourceLocation peekLocation() const { return m_nextToken.location; }
	std::string const& peekLiteral() const { return m_nextToken.literal; }
	///@}

	std::shared_ptr<std::string const> const& sourceName() const { return m_sourceName; }

	///@{
	///@name Error printing helper functions
	/// Functions that help pretty-printing parse errors
	/// Do only use in error cases, they are quite expensive.
	std::string lineAtPosition(int _position) const { return m_source.lineAtPosition(_position); }
	std::tuple<int, int> translatePositionToLineColumn(int _position) const { return m_source.translatePositionToLineColumn(_position); }
	///@}

private:
	/// Used for the current and look-ahead token and comments
	struct TokenDesc
	{
		Token::Value token;
		SourceLocation location;
		std::string literal;
		std::tuple<unsigned, unsigned> extendedTokenInfo;
	};

	// DEBUG //
	bool changeVariable = false;
	///////////


	///@{
	///@name Literal buffer support
	inline void addLiteralChar(char c) { m_nextToken.literal.push_back(c); }
	inline void addCommentLiteralChar(char c) { m_nextSkippedComment.literal.push_back(c); }
	inline void addLiteralCharAndAdvance() { addLiteralChar(m_char); advance(); }
	void addUnicodeAsUTF8(unsigned codepoint);
	///@}

	bool advance() { m_char = m_source.advanceAndGet(); return !m_source.isPastEndOfInput(); }
	void rollback(int _amount) { m_char = m_source.rollback(_amount); }

	inline Token::Value selectToken(Token::Value _tok) { advance(); return _tok; }
	/// If the next character is _next, advance and return _then, otherwise return _else.
	inline Token::Value selectToken(char _next, Token::Value _then, Token::Value _else);

	bool scanHexByte(char& o_scannedByte);
	bool scanUnicode(unsigned& o_codepoint);

	/// Scans a single Solidity token.
	void scanToken();

	/// Skips all whitespace and @returns true if something was skipped.
	bool skipWhitespace();
	/// Skips all whitespace except Line feeds and returns true if something was skipped
	bool skipWhitespaceExceptLF();
	Token::Value skipSingleLineComment();
	Token::Value skipMultiLineComment();

	void scanDecimalDigits();
	Token::Value scanNumber(char _charSeen = 0);
	std::tuple<Token::Value, unsigned, unsigned> scanIdentifierOrKeyword();

	Token::Value scanString();
	Token::Value scanHexString();
	Token::Value scanSingleLineDocComment();
	Token::Value scanMultiLineDocComment();
	/// Scans a slash '/' and depending on the characters returns the appropriate token
	Token::Value scanSlash();

	/// Scans an escape-sequence which is part of a string and adds the
	/// decoded character to the current literal. Returns true if a pattern
	/// is scanned.
	bool scanEscape();

	/// Return the current source position.
	int sourcePos() const { return m_source.position(); }
	bool isSourcePastEndOfInput() const { return m_source.isPastEndOfInput(); }

	TokenDesc m_skippedComment;  // desc for current skipped comment
	TokenDesc m_nextSkippedComment; // desc for next skiped comment

	TokenDesc m_currentToken;  // desc for current token (as returned by Next())
	TokenDesc m_nextToken;     // desc for next token (one token look-ahead)

	CharStream m_source;
	std::shared_ptr<std::string const> m_sourceName;

	/// one character look-ahead, equals 0 at end of input
	char m_char;
};

}
}
