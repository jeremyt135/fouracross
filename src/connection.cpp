#include "connectdisks/connection.hpp"
#include "connectdisks/gamelobby.hpp"
#include "type_utility.hpp"

#include <boost/endian/arithmetic.hpp>
#include <boost/endian/conversion.hpp>

#include <algorithm>
#include <iostream>
#include <functional>

using boost::asio::ip::address_v4;
using boost::asio::ip::tcp;

using namespace connectdisks::server;
using connectdisks::Board;
using connectdisks::ConnectDisks;

using typeutil::toUnderlyingType;
using typeutil::toScopedEnum;

std::shared_ptr<Connection> connectdisks::server::Connection::create(boost::asio::io_service & ioService, GameLobby* lobby)
{
	return std::shared_ptr<Connection>{new Connection{ioService, lobby}};
}

void connectdisks::server::Connection::onGameStart()
{
	// send id to client
	std::shared_ptr<server::Message> response{new server::Message{}};
	response->response = toScopedEnum<server::Response>::cast(
		boost::endian::native_to_big(toUnderlyingType(server::Response::gameStart)));

	// send the number of players
	response->data[0] = boost::endian::native_to_big(lobby->getNumPlayers());

	// send the board dimensions
	auto* game = lobby->getGame();
	const auto numCols = boost::endian::native_to_big(game->getNumColumns());
	const auto numRows = boost::endian::native_to_big(game->getNumRows());
	response->data[1] = numCols;
	response->data[2] = numRows;

	boost::asio::async_write(socket,
		boost::asio::buffer(response.get(), sizeof(server::Message)),
		std::bind(
			&Connection::handleWrite,
			this,
			response,
			std::placeholders::_1,
			std::placeholders::_2
		));
}

void connectdisks::server::Connection::onGameEnd()
{
	// tell client game has ended
	std::shared_ptr<server::Message> response{new server::Message{}};
	response->response = toScopedEnum<server::Response>::cast(
		boost::endian::native_to_big(toUnderlyingType(server::Response::gameEnd)));
	boost::asio::async_write(socket,
		boost::asio::buffer(response.get(), sizeof(server::Message)),
		std::bind(
			&Connection::handleWrite,
			this,
			response,
			std::placeholders::_1,
			std::placeholders::_2
		));
}

void connectdisks::server::Connection::waitForMessages()
{
#if defined DEBUG || defined _DEBUG
	std::cerr << "Connection " << this << " waiting to read message\n";
#endif
	// read a message from the client, handle in handleRead
	std::shared_ptr<client::Message> message{new client::Message{}};
	boost::asio::async_read(socket,
		boost::asio::buffer(message.get(), sizeof(client::Message)),
		std::bind(
			&Connection::handleRead,
			this,
			message,
			std::placeholders::_1,
			std::placeholders::_2
		));
}

void connectdisks::server::Connection::setId(Board::player_size_t id)
{
	if (this->id == 0)
	{
		this->id = id;

		// send id to client
		std::shared_ptr<server::Message> response{new server::Message{}};
		response->response = toScopedEnum<server::Response>::cast(
			boost::endian::native_to_big(toUnderlyingType(server::Response::connected)));
		response->data[0] = boost::endian::native_to_big(id);
		boost::asio::async_write(socket,
			boost::asio::buffer(response.get(), sizeof(server::Message)),
			std::bind(
				&Connection::handleWrite,
				this,
				response,
				std::placeholders::_1,
				std::placeholders::_2
			));
	}
}

connectdisks::Board::player_size_t connectdisks::server::Connection::getId() const noexcept
{
	return id;
}

void connectdisks::server::Connection::setGameLobby(GameLobby * lobby)
{
	this->lobby = lobby;
}

boost::asio::ip::tcp::socket& connectdisks::server::Connection::getSocket()
{
	return socket;
}

connectdisks::server::Connection::Connection(boost::asio::io_service & ioService, GameLobby* lobby) :
	lobby{lobby},
	socket{ioService},
	id{0}
{
}

void connectdisks::server::Connection::handleRead(std::shared_ptr<connectdisks::client::Message> message, const boost::system::error_code & error, size_t len)
{
#if defined DEBUG || defined _DEBUG
	std::cerr << "Connection " << this << " trying to read message\n";
#endif
	if (!error.failed())
	{

		if (len == 0)
		{
		#if defined DEBUG || defined _DEBUG
			std::cerr << "Connection " << this << " received 0 length message\n";
		#endif
			return;
		}

		const auto request{
			toScopedEnum<client::Response>::cast(
				boost::endian::big_to_native(toUnderlyingType(message->response))
			)
		};
		if (request == client::Response::ready)
		{
		#if defined DEBUG || defined _DEBUG
			std::cout << "Connection " << this << ": Client is ready\n";
		#endif
			handleClientReady();
		}
		waitForMessages();
	}
	else
	{
		switch (error.value())
		{
		case boost::asio::error::eof:
		case boost::asio::error::connection_aborted:
		case boost::asio::error::connection_reset:
		#if defined DEBUG || defined _DEBUG
			std::cerr << "Connection " << this << "::handleRead: client disconnected \n";
		#endif
			handleDisconnect();
			break;
		default:
		#if defined DEBUG || defined _DEBUG
			std::cerr << "Connection " << this << "::handleRead: " << error.message() << "\n";
		#endif
			break;
		}
	}

}

void connectdisks::server::Connection::handleWrite(std::shared_ptr<server::Message> message, const boost::system::error_code & error, size_t len)
{
	if (!error.failed())
	{
	#if defined DEBUG || defined _DEBUG
		std::cerr << "Sent message to client\n";
	#endif
	}
	else
	{
	#if defined DEBUG || defined _DEBUG
		std::cerr << "Connection::handleWrite: " << error.message() << "\n";
	#endif
	}
}

void connectdisks::server::Connection::handleDisconnect()
{
	lobby->onDisconnect(shared_from_this());
}

void connectdisks::server::Connection::handleClientReady()
{
	lobby->onReady(shared_from_this());
}