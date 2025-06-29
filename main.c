#include "wsserver.h"
#include <ctype.h>

char board[3][3];
int current_turn = 0; // 0 = Player X, 1 = Player O

void init_board()
{
    int k = 1;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            board[i][j] = ' ';
}

void send_board(int client_fd1, int client_fd2)
{
    char message[128] = {0};
    snprintf(message, sizeof(message),
             "BOARD\n%c|%c|%c\n%c|%c|%c\n%c|%c|%c\nTURN:%c",
             board[0][0], board[0][1], board[0][2],
             board[1][0], board[1][1], board[1][2],
             board[2][0], board[2][1], board[2][2],
             current_turn == 0 ? 'X' : 'O');
    printf("Sending board to clients:\n%s\n", message);
    ws_send(client_fd1, message, strlen(message));
    ws_send(client_fd2, message, strlen(message));
}

int make_move(char symbol, int pos)
{
    int row = (pos - 1) / 3;
    int col = (pos - 1) % 3;
    if (pos < 1 || pos > 9 || board[row][col] != ' ')
        return 0;
    board[row][col] = symbol;
    return 1;
}

char check_winner()
{
    char lines[8][3] = {
        {board[0][0], board[0][1], board[0][2]},
        {board[1][0], board[1][1], board[1][2]},
        {board[2][0], board[2][1], board[2][2]},
        {board[0][0], board[1][0], board[2][0]},
        {board[0][1], board[1][1], board[2][1]},
        {board[0][2], board[1][2], board[2][2]},
        {board[0][0], board[1][1], board[2][2]},
        {board[0][2], board[1][1], board[2][0]}};
    for (int i = 0; i < 8; i++)
        if (lines[i][0] != ' ' && lines[i][0] == lines[i][1] && lines[i][1] == lines[i][2])
            return lines[i][0];
    return ' ';
}

int check_draw() {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (board[i][j] == ' ') return 0;
    return 1;
}

void wait_for_second_player(int server_fd, int *player2_fd)
{
    printf("Waiting for Player 2...\n");
    *player2_fd = ws_accept_client(server_fd);
    if (!ws_handshake(*player2_fd))
        return;
    ws_send(*player2_fd, "You are Player O", 16);
    printf("New Player O socket: %d\n", *player2_fd);
    send_board(current_turn == 0 ? *player2_fd : *player2_fd, *player2_fd);
}

int main()
{
    int port = atoi(getenv("PORT"));
    int server_fd = ws_create_server(port);
    if (server_fd < 0)
    {
        perror("Server creation failed");
        return 1;
    }

    printf("Waiting for Player 1...\n");
    int client1 = ws_accept_client(server_fd);
    if (!ws_handshake(client1))
        return 1;
    ws_send(client1, "You are Player X", 16);

    int client2 = -1;
    wait_for_second_player(server_fd, &client2);

    printf("Both players connected!\n");
    printf("Player X socket: %d\n", client1);
    printf("Player O socket: %d\n", client2);
    init_board();
    current_turn = 0;
    send_board(client1, client2);

    char buffer[1024];
    int restart1 = 0, restart2 = 0;
    while (1)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client1, &read_fds);
        if (client2 != -1)
            FD_SET(client2, &read_fds);

        int max_fd = (client1 > client2) ? client1 : client2;

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0)
        {
            perror("select error");
            break;
        }

        int sender_fd = FD_ISSET(client1, &read_fds) ? client1 : (client2 != -1 && FD_ISSET(client2, &read_fds) ? client2 : -1);
        if (sender_fd == -1)
            continue;

        int len = ws_recv(sender_fd, buffer, sizeof(buffer));
        if (len <= 0)
        {
            if (sender_fd == client1)
                client1 = -1;
            else if (sender_fd == client2)
                client2 = -1;
            continue;
        }
        buffer[len] = 0;

        if (strncmp(buffer, "QUIT", 4) == 0)
        {
            printf("Player on socket %d quit.\n", sender_fd);
            int other_fd = (sender_fd == client1) ? client2 : client1;
            printf("Other player socket: %d\n", other_fd);
            if (other_fd != -1)
            {
                ws_send(other_fd, "PLAYER_QUIT", 12);
            }
            if (sender_fd == client1)
                client1 = -1;
            else
                client2 = -1;
            
        }
        
        if (client1 == -1 || client2 == -1)
        {
            if (client1 == -1)
            {
                printf("Waiting for new Player X...\n");
                client1 = ws_accept_client(server_fd);
                if (!ws_handshake(client1))
                    continue;
                ws_send(client1, "You are Player X", 16);
            }
            if (client2 == -1)
            {
                printf("Waiting for new Player O...\n");
                client2 = ws_accept_client(server_fd);
                if (!ws_handshake(client2))
                    continue;
                ws_send(client2, "You are Player O", 16);
            }
            init_board();
            current_turn = 0;
            send_board(client1, client2);
        }

        if (strncmp(buffer, "RESTART", 7) == 0)
        {
            if (sender_fd == client1)
                restart1 = 1;
            else if (sender_fd == client2)
                restart2 = 1;

            if (restart1 && restart2)
            {
                printf("Both players requested restart.\n");
                init_board();
                current_turn = 0;
                send_board(client1, client2);
                restart1 = restart2 = 0;
            }
            
        }
        

        int expected_fd = (current_turn == 0) ? client1 : client2;
        if (sender_fd != expected_fd)
        {
            ws_send(sender_fd, "Not your turn", 13);
            continue;
        }

        int pos = atoi(buffer);
        if (make_move(current_turn == 0 ? 'X' : 'O', pos))
        {
            char winner = check_winner();
            if (winner != ' ')
            {
                char win_msg[32];
                snprintf(win_msg, sizeof(win_msg), "WINNER: %c", winner);
                send_board(client1, client2);
                if (client1 != -1)
                    ws_send(client1, win_msg, strlen(win_msg));
                if (client2 != -1)
                    ws_send(client2, win_msg, strlen(win_msg));
                continue;
            }
            else if (check_draw()) {
                printf("Game is a draw.\n");
                char draw_msg[] = "DRAW";
                send_board(client1, client2);
                if (client1 != -1) ws_send(client1, draw_msg, strlen(draw_msg));
                if (client2 != -1) ws_send(client2, draw_msg, strlen(draw_msg));
                continue;
            }
            current_turn = 1 - current_turn;
            send_board(client1, client2);
        }
        else
        {
            ws_send(sender_fd, "Invalid move. Try again.", 25);
        }
    }

    if (client1 != -1)
        close(client1);
    if (client2 != -1)
        close(client2);
    close(server_fd);
    return 0;
}
