/** @file robotdriver.cpp
 *  @brief The robotdriver.cpp is a file that contains the specific progress realized.
 *  
 *  @date <March.27.2015>
 *  @version <2.0>
 *
 */

#include "robotdriver.h"
#include <iostream>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <jpeglib.h>


/** @brief Open the socket to the webserver */
int riOpen(RobotIfType *ri, bool game_server) {
    int sock;
    struct timeval tv;
    struct sockaddr * sa;

    // Set up the socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    ri->sock = sock;
    if(sock == -1) {
        perror("Unable to create socket.");
        return RI_RESP_FAILURE;
    }
    // Set the timeout
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv))) {
        perror("Unable to set socket timeout.");
        return RI_RESP_FAILURE;
    }

#ifdef DEBUG_CONNECT
    printf("Setup socket\n");
#endif

    // Connect to the robot
    if(game_server)
        sa = (struct sockaddr *)&(ri->game_server_addr);
    else
        sa = (struct sockaddr *)&(ri->server_addr);

    if(connect(sock, sa, sizeof(struct sockaddr)) == -1) {
        perror("Unable to connect to the server");
        return RI_RESP_FAILURE;
    }
#ifdef DEBUG_CONNECT
    printf("Made connection\n");
#endif
    return RI_RESP_SUCCESS;
}

/** @brief Close the webserver socket */
int riClose(RobotIfType *ri) {
    close(ri->sock);
    ri->sock = -1;
    return RI_RESP_SUCCESS;
}

/** @brief Make an http request */
#ifdef DEBUG_DUMP_RESP
int resp_cnt = 0;
#endif

int httpRequest(RobotIfType *ri, char *cmd, char *response, int rlen, bool game_server) {
    int data_recvd = 0;
    char *start_of_data;
    char *cl;
    char req[512];
    int data_sz = 0;

    // Clear the buffers
    memset(response, 0, rlen);
    memset(req, 0, 512);

    // Make the request
    riOpen(ri, game_server);
    sprintf(req, "GET  /%s HTTP/1.0\nUser-Agent: RovioCMD 1.0\n\n", cmd);
#ifdef DEBUG_REQ
    printf("%s", req);
#endif
    send(ri->sock, req, strlen(req), 0);

    // Receive the header (and some data)
    // Hope that the \r\n\r\n isn't in the break
    data_recvd = recv(ri->sock, req, 512, SOCK_NONBLOCK);
    start_of_data = strstr(req, "\r\n\r\n");
    // We didn't actually recv the header, bail out
    if(start_of_data == NULL)
        return 0;
    // Move to the end of the header string
    start_of_data += 4;

#ifdef DEBUG_RESP
    printf("Data Recvd: %i Start of data: %i\n", data_recvd, (int) (start_of_data - req));
#endif

    // Find the end of the header
    data_recvd = data_recvd - (start_of_data - req);
    memcpy(response, start_of_data, data_recvd);

    // Get the size of the data
    cl = strstr(req, "Content-Length");
    if(cl == NULL)
        data_sz = data_recvd;
    else
        sscanf(cl, "Content-Length: %i\n", &data_sz);

#ifdef DEBUG_RESP
    printf("Size: %i\n", data_sz);
#endif

    // Fetch the rest of the data (if needed)
    while(data_recvd < data_sz)
        data_recvd += recv(ri->sock, response + data_recvd, rlen - data_recvd, 0);

#ifdef DEBUG_RESP
    // Print the received response
    printf("Response:\n%s\n", response);
#endif

#ifdef DEBUG_DUMP_RESP
    FILE *f;
    sprintf(req, "resp%i", resp_cnt);
    f = fopen(req, "w");
    fwrite(response, 1, data_sz, f);
    fclose(f);
    resp_cnt++;
#endif

    riClose(ri);
    return data_sz;
}

/** @brief  I2C Hacking */
static int riReadI2C(RobotIfType *ri, unsigned int addr, unsigned int *value) {
    char response[512];
    char cmd[128];
    char *start_of_resp;

    // Send the read command
    sprintf(cmd, "debug.cgi?action=read_i2c&address=0x%x", addr);
    httpRequest(ri, cmd, response, 512, false);
    start_of_resp = strstr(response, "read_i2c = ");
    if(start_of_resp == NULL)
        return RI_RESP_FAILURE;

    sscanf(start_of_resp + 12, "%*x=%x", value);
    return RI_RESP_SUCCESS;
}

static int riWriteI2C(RobotIfType *ri, unsigned int addr, unsigned int value) {
    char response[512];
    char cmd[128];
    char *start_of_resp;
    int result = 0;

    // Send the write command
    sprintf(cmd, "debug.cgi?action=write_i2c&address=0x%x&value=0x%x", addr, value);
    httpRequest(ri, cmd, response, 512, false);
    start_of_resp = strstr(response, "write_i2c = ");
    if(start_of_resp == NULL)
        return RI_RESP_FAILURE;

    sscanf(start_of_resp + 12, "%*x=%x", &result);
    if(result != value)
        return RI_RESP_FAILURE;

    return RI_RESP_SUCCESS;
}



/** @brief Get sensor data from the Rovio */
int riGetSensorData(RobotIfType *ri, RIData *sensor) {
    char cmd[32];
    char response[512];
    char *start_of_resp;

    sprintf(cmd, "rev.cgi?Cmd=nav&action=20");
    httpRequest(ri, cmd, response, 512, false);
    memset(sensor, 0, sizeof(RIData));
    start_of_resp = strstr(response, "responses = ");
    if(start_of_resp == NULL)
        return RI_RESP_FAILURE;
    sscanf(start_of_resp + 12, "%2X%2X%2X%4X%2X%4X%2X%4X%2X%2X%2X%2X",
        &(sensor->length),
        &(sensor->unused),
        &(sensor->left_wheel_dir),
        &(sensor->left_wheel_enc_ticks),
        &(sensor->right_wheel_dir),
        &(sensor->right_wheel_enc_ticks),
        &(sensor->rear_wheel_dir),
        &(sensor->rear_wheel_enc_ticks),
        &(sensor->unused2),
        &(sensor->head_position),
        &(sensor->battery),
        &(sensor->status)
    );
#ifdef DEBUG_SENSOR
    printf("Length: %X\n", sensor->length);
    printf("Status: %X\n", sensor->status);
#endif
    return RI_RESP_SUCCESS;
}

/** @brief Enable/Disable IR */
int riIR(RobotIfType *ri, int status) {
    char cmd[32];
    char response[512];
    char *start_of_resp;
    int result;

    sprintf(cmd, "rev.cgi?Cmd=nav&action=19&IR=%i", status);
    httpRequest(ri, cmd, response, 512, false);
    start_of_resp = strstr(response, "responses = ");
    if(start_of_resp == NULL)
        return RI_RESP_FAILURE;
    sscanf(start_of_resp + 12, "%i", &result);

    return result;
}

/** @brief Get sensor report from the Rovio (North Star) */
int riGetStatus(RobotIfType *ri, RIReport *report) {
    char cmd[32];
    char response[512];
    char *start_of_resp;
    int result;

    // GetReport()
    sprintf(cmd, "rev.cgi?Cmd=nav&action=1");
    httpRequest(ri, cmd, response, 512, false);
    start_of_resp = strstr(response, "responses = ");
    if(start_of_resp == NULL)
        return RI_RESP_FAILURE;

    // Parse the reports
    sscanf(start_of_resp + 12, "%i|x=%i|y=%i|theta=%f|room=%i|ss=%i |beacon=%*i|beacon_x=%*i|next_room=%*i|next_room_ss=%*i |state=%i|ui_status=%*i|resistance=%*i|sm=%*i|pp=%*i|flags=%*X |brightness=%i|resolution=%i|video_compression=%*i|frame_rate=%i |privilege=%*i|user_check=%*i|speaker_volume=%i|mic_volume=%i |wifi_ss=%i|show_time=%*i|ddns_state=%*i|email_state=%*i |battery=%i|charging=%i|head_position=%i|ac_freq=%*i",
        &result,
        &(report->x),
        &(report->y),
        &(report->theta),
        &(report->room_id),
        &(report->strength),
        &(report->state),
        &(report->brightness),
        &(report->cam_res),
        &(report->frame_rate),
        &(report->speaker_volume),
        &(report->mic_volume),
        &(report->wifi),
        &(report->battery),
        &(report->charging),
        &(report->head_position)
    );

#ifdef DEBUG_STATUS
    printf("Result: %i, X,Y: (%i, %i), Theta: %f Brightness: %i Battery: %i Head Position: %i\n", result, report->x, report->y, report->theta, report->brightness, report->battery, report->head_position);
#endif

    return result;
}

/** @brief Resets the encoder counts */
void riResetState(RobotIfType *ri) {
    ri->right_wheel_enc = 0;
    ri->left_wheel_enc = 0;
    ri->rear_wheel_enc = 0;

    memset(&(ri->sensor), 0, sizeof(RIData));
    memset(&(ri->report), 0, sizeof(RIReport));
    return;
}

/** @brief Setup the robot*/
    int riSetup(RobotIfType *ri, const char *address, int robot_id) {
    struct hostent *host;
    struct in_addr **addr_list;

    // Make a copy of the address in case we'll need it later
    strncpy(ri->address, address, MAX_ADDR_LEN);

    // Get the host by the name passed in (in setup, so we don't have to make so many DNS calls
    host = gethostbyname(address);
    if(host == NULL) {
        ri->sock = -1;
        perror("Unable to find the specified host.");
        return RI_RESP_FAILURE;
    }

    // Address List
    addr_list = (struct in_addr **)host->h_addr_list;
#ifdef DEBUG_SETUP
        for(i = 0; addr_list[i] != NULL; i++) {
            printf("Address %i: %s\n", i, inet_ntoa(*addr_list[i]));
    }
#endif

    /* Set up the server address */
    ri->server_addr.sin_family = AF_INET;
    ri->server_addr.sin_port = htons(80);
    ri->server_addr.sin_addr = *((struct in_addr *)host->h_addr);
    bzero(&(ri->server_addr.sin_zero), 8);

    // Now we do the same for the game server address */
    strncpy(ri->gs_address, DEFAULT_SERVER_NAME, MAX_ADDR_LEN);

    // Get the host by the name passed in (in setup, so we don't have to make so many DNS calls
//   host = gethostbyname(ri->gs_address);
    if(host == NULL) {
        ri->sock = -1;
        perror("Unable to find the specified host.");
        return RI_RESP_FAILURE;
    }

    // Address List
    addr_list = (struct in_addr **)host->h_addr_list;
#ifdef DEBUG_SETUP
        for(i = 0; addr_list[i] != NULL; i++) {
            printf("Address %i: %s\n", i, inet_ntoa(*addr_list[i]));
    }
#endif

    /* Set up the server address */
    ri->game_server_addr.sin_family = AF_INET;
    ri->game_server_addr.sin_port = htons(80);
    ri->game_server_addr.sin_addr = *((struct in_addr *)host->h_addr);
    bzero(&(ri->game_server_addr.sin_zero), 8);

    // Random seed
    srand(time(NULL));

    // Reset all of the counters
    riResetState(ri);

    // Make sure the IR Light is on
    riIR(ri, RI_LIGHT_ON);

    // Set the robot number
    ri->id = robot_id;

    return RI_RESP_SUCCESS;
}

    /** @brief Robot Movement, get  wheel's encoder*/
int riGetWheelEncoder(RobotIfType *ri, int wheel) {
    // Get the desired wheel
    switch(wheel) {
    case RI_WHEEL_LEFT:
        if(RI_WHEEL_MASK(ri->sensor.left_wheel_dir) == RI_WHEEL_FORWARD)
            return (ri->sensor.left_wheel_enc_ticks);
        else
            return (-(ri->sensor.left_wheel_enc_ticks));
    case RI_WHEEL_RIGHT:
        if(RI_WHEEL_MASK(ri->sensor.right_wheel_dir) == RI_WHEEL_FORWARD)
            return (ri->sensor.right_wheel_enc_ticks);
        else
            return (-(ri->sensor.right_wheel_enc_ticks));
    case RI_WHEEL_REAR:
        if(RI_WHEEL_MASK(ri->sensor.rear_wheel_dir) == RI_WHEEL_FORWARD)
            return (ri->sensor.rear_wheel_enc_ticks);
        else
            return (-(ri->sensor.rear_wheel_enc_ticks));
    default:
        return 0;
    }

    // Shouldn't get here...
    return 0;
}
/** @brief Robot Movement, get wheel's encoder totally*/
int riGetWheelEncoderTotals(RobotIfType *ri, int wheel) {
    // Get the desired wheel
    switch(wheel) {
    case RI_WHEEL_LEFT:
        return ri->left_wheel_enc;
    case RI_WHEEL_RIGHT:
        return ri->right_wheel_enc;
    case RI_WHEEL_REAR:
        return ri->rear_wheel_enc;
    default:
        return -1;
    }

    // Shouldn't get here...
    return -1;
}


/** @brief Update all sensor data*/
int riUpdate(RobotIfType *ri) {
    int ret;

    ret = riGetSensorData(ri, &(ri->sensor));
    if(ret != RI_RESP_SUCCESS)
        return ret;

    ret = riGetStatus(ri, &(ri->report));
    if(ret != RI_RESP_SUCCESS)
        return ret;

    ri->right_wheel_enc += riGetWheelEncoder(ri, RI_WHEEL_RIGHT);
    ri->left_wheel_enc += riGetWheelEncoder(ri, RI_WHEEL_LEFT);
    ri->rear_wheel_enc += riGetWheelEncoder(ri, RI_WHEEL_REAR);

    return ret;
}

/** @brief Finds a cosine of angle between vectors from pt0->pt1 and from pt0->pt2*/
double riAngle(CvPoint* pt1, CvPoint* pt2, CvPoint* pt0) {
    double dx1 = pt1->x - pt0->x;
    double dy1 = pt1->y - pt0->y;
    double dx2 = pt2->x - pt0->x;
    double dy2 = pt2->y - pt0->y;
    return (dx1*dx2 + dy1*dy2)/sqrt((dx1*dx1 + dy1*dy1)*(dx2*dx2 + dy2*dy2) + 1e-10);
}


/**
 * @param The  first parameter is a string containing the name or address of the robot,The second parameter, robot_id, is used for Rovio-Man to   identify the robot, otherwise, set it to 0.
 * @exception none
 * @note   This will be resolved and cached for later usage. This function also turns on the IR sensor, as it is generally useful to have on. All of the following functions are methods of this class.
 */
RobotInterface::RobotInterface(const char* address, int robot_id) {
	// Clear the robot interface struct
    memset(&ri, 0, sizeof(RobotIfType));

	// Configure the robot interface
    if(riSetup(&ri, address, robot_id)) {
		std::cout << "Unable to configure the robot interface" << std::endl;
		return;	
	}
	return;
}

RobotInterface::~RobotInterface(){
	return;
}

/**
 *@return The API Version
 */
// Robot API version
void RobotInterface::APIVersion(int* major, int* minor) {
    // Robot API v2.0
    *major = 2;
    *minor = 0;
}

/**********************************************************
 * Movement control
 **********************************************************/

/**
 * @param movement the robot to move in the direction specified .
 * @param speed speed between 1 and 10, with 1 being the fastest (defined as RI_FASTEST) and 10 being the slowest (defined as RI_SLOWEST).
 * @note This function causes the robot to move in the direction specified by the movement code, at a speed between 1 and 10.
 * <pre>
 * Movement Code and 	Behavior
 * RI_STOP 	Stops any current movement
 * RI_MOVE_FORWARD 	Moves the robot forward
 * RI_MOVE_BACKWARD 	Moves the robot backward
 * RI_MOVE_LEFT 	Moves the robot to the left
 * RI_MOVE_RIGHT 	Moves the robot to the right
 * RI_TURN_LEFT 	Turns the robot to the left
 * RI_TURN_RIGHT 	Turns the robot to the right
 * RI_MOVE_FWD_LEFT 	Moves the robot forward and left at the same time
 * RI_MOVE_FWD_RIGHT 	Moves the robot forward and right at the same time
 * RI_MOVE_BACK_LEFT 	Moves the robot backward and left at the same time
 * RI_MOVE_BACK_RIGHT 	Moves the robot backward and right at the same time
 * RI_TURN_LEFT_20DEG 	Rotates the robot 20 degrees to the left
 * RI_TURN_RIGHT_20DEG 	Rotates the robot 20 degrees to the right
 * RI_HEAD_UP 	Moves the head to the highest position
 * RI_HEAD_MIDDLE 	Moves the head to the middle position
 * RI_HEAD_DOWN 	Moves the head to the lowest position
 * </pre>
 */
int RobotInterface::move(int movement, int speed) {
    char response[512];
    char cmd[128];
    char *start_of_resp;
    int result;

    // Send the move command
    sprintf(cmd, "rev.cgi?Cmd=nav&action=18&drive=%i&speed=%i", movement, speed);
    httpRequest(&ri, cmd, response, 512, false);
    start_of_resp = strstr(response, "responses = ");
    if(start_of_resp == NULL)
        return RI_RESP_FAILURE;
    sscanf(start_of_resp + 12, "%i", &result);

    return result;
}

/**
 * @note This function forces the robot to go back to the saved home location and dock for charging.
 */
int RobotInterface::goHome(void) {
    char cmd[32];
    char response[512];
    char *start_of_resp;
    int result;

    // GoHomeAndDock
    sprintf(cmd, "rev.cgi?Cmd=nav&action=13");
    httpRequest(&ri, cmd, response, 512, false);
    start_of_resp = strstr(response, "responses = ");
    if(start_of_resp == NULL)
        return RI_RESP_FAILURE;
    sscanf(start_of_resp + 12, "%i", &result);
    return result;

}

/**
 * @param wheel specify the wheel using one of the left, right, or rear.
 * @return the move direction of the wheel.
 * @note RI_WHEEL_FORWARD if the wheel was moving in a forward direction and RI_WHEEL_BACKWARD if the wheel was moving in a backward direction.
 */
int RobotInterface::getWheelDirection(int wheel) {
    // Get the desired wheel
    switch(wheel) {
    case RI_WHEEL_LEFT:
        return (RI_WHEEL_MASK((&ri)->sensor.left_wheel_dir));
    case RI_WHEEL_RIGHT:
        return (RI_WHEEL_MASK((&ri)->sensor.right_wheel_dir));
    case RI_WHEEL_REAR:
        return (RI_WHEEL_MASK((&ri)->sensor.rear_wheel_dir));
    default:
        return -1;
    }
    // Shouldn't get here...
    return -1;
}

/**
 * @param wheel specify the wheel using one of the left, right, or rear.
 * @return the movement of the requested wheel encoder since the last sensor read.
 * @note  Forward movement is represented with a positive number, while backward movement is represented with a negative number. Four ticks of an encoder is about 1cm.
 */
int RobotInterface::getWheelEncoder(int wheel) {
    // Get the desired wheel
    switch(wheel) {
    case RI_WHEEL_LEFT:
        if(RI_WHEEL_MASK((&ri)->sensor.left_wheel_dir) == RI_WHEEL_FORWARD)
            return ((&ri)->sensor.left_wheel_enc_ticks);
        else
            return (-((&ri)->sensor.left_wheel_enc_ticks));
    case RI_WHEEL_RIGHT:
        if(RI_WHEEL_MASK((&ri)->sensor.right_wheel_dir) == RI_WHEEL_FORWARD)
            return ((&ri)->sensor.right_wheel_enc_ticks);
        else
            return (-((&ri)->sensor.right_wheel_enc_ticks));
    case RI_WHEEL_REAR:
        if(RI_WHEEL_MASK((&ri)->sensor.rear_wheel_dir) == RI_WHEEL_FORWARD)
            return ((&ri)->sensor.rear_wheel_enc_ticks);
        else
            return (-((&ri)->sensor.rear_wheel_enc_ticks));
    default:
        return 0;
    }

    // Shouldn't get here...
    return 0;
}

/**
 * @param wheel specify the wheel using one of the left, right, or rear.
 * @return The total movement of the requested wheel encoder since the last reset or initialization.
 * @note  Forward overall movement is represented with a positive number, while backward overall movement is represented with a negative number. Four ticks of an encoder is about 1cm.
 */
int RobotInterface::getWheelEncoderTotals(int wheel) {
    // Get the desired wheel
    switch(wheel) {
    case RI_WHEEL_LEFT:
        return (&ri)->left_wheel_enc;
    case RI_WHEEL_RIGHT:
        return (&ri)->right_wheel_enc;
    case RI_WHEEL_REAR:
        return (&ri)->rear_wheel_enc;
    default:
        return -1;
    }

    // Shouldn't get here...
    return -1;
}

/**
 * @return the head position.
 * @note Head Code RI_ROBOT_HEAD_LOW  RI_ROBOT_HEAD_MID  RI_ROBOT_HEAD_HIGH.
 */
int RobotInterface::getHeadPosition(void) {
    // FIXME: Check these values against the robot
    if((&ri)->sensor.head_position >= 204)
        return RI_ROBOT_HEAD_LOW;
    else if((&ri)->sensor.head_position >= 135 && (&ri)->sensor.head_position < 140)
        return RI_ROBOT_HEAD_MID;
    else if((&ri)->sensor.head_position <= 65)
        return RI_ROBOT_HEAD_HIGH;
    else
        printf("Unknown head position: %i\n", (&ri)->sensor.head_position);
    return RI_RESP_FAILURE;
}

/**********************************************************
 * IR and Headlights
 **********************************************************/
/**
 * @param status Status controls the IR sensor, with RI_LIGHT_ON turning on the IR detector, and RI_LIGHT_OFF turning off the detector.
 * @note The IR and headlight can be turned on and off.
 */
int RobotInterface::IR(int status) {
    char cmd[32];
    char response[512];
    char *start_of_resp;
    int result;

    sprintf(cmd, "rev.cgi?Cmd=nav&action=19&IR=%i", status);
    httpRequest((&ri), cmd, response, 512, false);
    start_of_resp = strstr(response, "responses = ");
    if(start_of_resp == NULL)
        return RI_RESP_FAILURE;
    sscanf(start_of_resp + 12, "%i", &result);

    return result;
}

/**
 * @return  true if the cached sensor data indicates there is an object blocking the robot, false otherwise.
 * @note  The IR can be used to let the programmer know if an object is blocking the path of the Rovio robot.
 */
bool RobotInterface::IRDetected(void) {
     if((&ri)->sensor.status & RI_STATUS_IR_DETECTOR)
         return true;
     else
        return false;
}

/**
 * @return RI_RESP_SUCCESS.
 * @note Store the result of IR detector,reserve1 if there is obstacle, reserve 0 if there is no obstacle.
 */
int RobotInterface::IRStatus(void){
     int sta;
     if(IRDetected())
         sta = 1;
     else
         sta = 0;
  //printf the sta with the result by IRDetected
     printf("%d\n",sta);
  //reserve the 1 or 0 in a file that you can name it
     FILE *fp = NULL;
     char fname[32];
     char filename[50];
     printf("Please enter the file's name\n");
     scanf("%s",fname);
     strcat(fname,".txt");
     sprintf(filename,"/home/zzy/Rovio-image/%s",fname);
     fp = fopen(filename,"a+");
     if(fp==NULL){
         printf("file open failed!");
         return -1;
     }
     fprintf(fp,"%d\n",sta);
     fclose(fp);
return RI_RESP_SUCCESS;
}

/**
 * @param status Status controls the headlight, with RI_LIGHT_ON turning on the headlight, and RI_LIGHT_OFF turning off the headlight.
 */
int RobotInterface::Headlight(int status) {
    char cmd[32];
    char response[512];
    char *start_of_resp;
    int result;

    sprintf(cmd, "rev.cgi?Cmd=nav&action=19&LIGHT=%i", status);
    httpRequest(&ri, cmd, response, 512, false);
    start_of_resp = strstr(response, "responses = ");
    if(start_of_resp == NULL)
        return RI_RESP_FAILURE;
    sscanf(start_of_resp + 12, "%i", &result);

    return result;
}

/**********************************************************
 * North Star Interface
 **********************************************************/
/**
 * @return the X coordinate of the robot from the North Star system.
 * @note The coordinates are relative to the base. One cm is equivalent to about 45 ticks. This value can range from -32767 to 32768 and represents the X coordinate on a grid surrounding the point the base station defines.
 */
int RobotInterface::X(void) {
       return (&ri)->report.x;
}
/**
 * @return the Y coordinate of the robot from the North Star system.
 * @note The coordinates are relative to the base. One cm is equivalent to about 45 ticks. This value can range from -32767 to 32768 and represents the Y coordinate on a grid surrounding the point the base station defines.
 */
int RobotInterface::Y(void) {
        return (&ri)->report.y;
}
/**
 * @return the direction of the robot from the North Star system.
 * @note The coordinates are relative to the base. One cm is equivalent to about 45 ticks.This value can range from -PI to PI and represents the direction the robot is pointing, relative to the point the base station defines.
 */
float RobotInterface::Theta(void) {
     return (&ri)->report.theta;
}
/**
 * @return the signal strength of the North Star system indicated by the RoomID.
 * @note  The value returned will be within the ranges below:
 * <pre>
 *      RI_ROBOT_NAV_SIGNAL_STRONG
 *      RI_ROBOT_NAV_SIGNAL_MID
 *      RI_ROBOT_NAV_SIGNAL_WEAK
 *      RI_ROBOT_NAV_SIGNAL_NO_SIGNAL
 * </pre>
 */
int RobotInterface::NavStrength(void) {
    if((&ri)->report.strength > 47000)
        return RI_ROBOT_NAV_SIGNAL_STRONG;
    else if((&ri)->report.strength > 20000)
        return RI_ROBOT_NAV_SIGNAL_MID;
    else if((&ri)->report.strength > 5000)
        return RI_ROBOT_NAV_SIGNAL_WEAK;
    else // ri->report.strength < 5000
        return RI_ROBOT_NAV_SIGNAL_NO_SIGNAL;
}
/**
 * @return the signal strength of the North Star system indicated by the RoomID.
 * @note The value returned is not interpreted, but ranges are provided below:
 * <pre>
 * RI_ROBOT_NAV_SIGNAL_STRONG 	> 47000
 * RI_ROBOT_NAV_SIGNAL_MID 	> 20000
 * RI_ROBOT_NAV_SIGNAL_WEAK 	> 5000
 * RI_ROBOT_NAV_SIGNAL_NO_SIGNAL 	<= 5000
 * </pre>
 */
int RobotInterface::NavStrengthRaw(void) {
     return (&ri)->report.strength;
}
/**
 * @return The current battery life of the robot.
 * @note Battery Life Constant's Meaning:
 * <pre>
 * RI_ROBOT_BATTERY_OFF 	Robot will turn itself off if the battery life is reported below this level
 * RI_ROBOT_BATTERY_HOME 	Robot will try to go home if the battery life is reported below this level
 * RI_ROBOT_BATTERY_MAX 	Robot is operating under normal conditions below this level
 * </pre>
 */
int RobotInterface::Battery(void) {
    return (&ri)->report.battery;
}
/**
 * @return The wifi strength.
 * @note This value is between 0 and 254. A larger value indicates a better connection.
 */
int RobotInterface::WifiStrengthRaw(void) {
    return (&ri)->report.wifi;
}
/**
 * @return the beacon ID of the North Star system with the strongest signal.
 * @note This value will be between 0 and 9.
 */
int RobotInterface::RoomID(void) {
       return (&ri)->report.room_id;
}

/**********************************************************
 * Copies of the Raw Reports - Remember to delete these!
 **********************************************************/
/**
 * @return The sensor data from the Rovio.
 * @note
 * <pre>
 * {
 * unsigned int		length;
 * unsigned int		unused;
 * unsigned int 		left_wheel_dir;
 * unsigned int 		left_wheel_enc_ticks;
 *  unsigned int		right_wheel_dir;
 *  unsigned int		right_wheel_enc_ticks;
 *  unsigned int		rear_wheel_dir;
 *  unsigned int		rear_wheel_enc_ticks;
 *  unsigned int		unused2;
 *  unsigned int		head_position;
 *  unsigned int		battery;
 *  unsigned int		status;
 * }ri_data_t
 * </pre>
 */
RIData *RobotInterface::getSensors(void) {
    RIData *s = new RIData;
    memcpy(s, &(ri.sensor), sizeof(RIData));
	return s;
}
/**
 * @return The report from the Rovio.
 * @note
 * <pre>
 * {
 * // Rovio location in relation to the room beacon
* int			x, y; // -32767 to 32768
* float			theta; // -PI to PI
* unsigned int		room_id; //0 = Home base, 1 - 9 other rooms
* unsigned int		strength; //Navigation signal strength: 0 - 65535: Strong > 47000, No Signal < 5000
* // Robot operational state
* unsigned int		state; //RI_ROBOT_STATE below, current robot state \
* // Camera controls
* unsigned int		brightness; // Brightness of the camera
* unsigned int		cam_res; // Camera Resolution
* unsigned int		frame_rate; // Camera frame rate
* unsigned int		speaker_volume; // Speaker volume
* unsigned int		mic_volume; // Microphone volume
* // Wifi info
* unsigned int		wifi; // Wifi signal strength
* // Battery charge info
* unsigned int		battery; // Battery level
* unsigned int		charging; // Battery charging
* // Head position
* unsigned int		head_position; // Head position
* }ri_report_t
* </pre>
 */
RIReport *RobotInterface::getReport(void) {
    RIReport *r = new RIReport;
    memcpy(r, &(ri.report), sizeof(RIReport));
	return r;
}

// Update the sensor and report data
/**
 * @return update the cached sensor data with the current values.
 */
int RobotInterface::update(void) {
    int ret;

    ret = riGetSensorData(&ri, &((&ri)->sensor));
    if(ret != RI_RESP_SUCCESS)
        return ret;

    ret = riGetStatus(&ri, &((&ri)->report));
    if(ret != RI_RESP_SUCCESS)
        return ret;

    (&ri)->right_wheel_enc += riGetWheelEncoder((&ri), RI_WHEEL_RIGHT);
    (&ri)->left_wheel_enc += riGetWheelEncoder((&ri), RI_WHEEL_LEFT);
    (&ri)->rear_wheel_enc += riGetWheelEncoder((&ri), RI_WHEEL_REAR);

    return ret;
}

// Reset the cached data
/**
 * @note resets the cumulative wheel encoder totals in the robot instance.
 */
void RobotInterface::resetState(void) {
    (&ri)->right_wheel_enc = 0;
    (&ri)->left_wheel_enc = 0;
    (&ri)->rear_wheel_enc = 0;

    memset(&((&ri)->sensor), 0, sizeof(RIData));
    memset(&((&ri)->report), 0, sizeof(RIReport));
    return;
}

/**********************************************************
 * Camera Interface
 **********************************************************/
#include "jpegworkaround.h"

// Get a jpeg image from the robot
/**
 * @param image image is IplImage format used by OpenCV.
 * @return Captures an image from the camera in the IplImage format used by OpenCV.
 * @note The image must be pre-allocated. The captured image is in BGR format.
 */
int RobotInterface::getImage(IplImage *image) {
    char cmd[32];
    unsigned char *response;
    int data_sz, row;
#ifdef JPEG_NO_MEMCPY
    CvScalar s;
    int col;
#endif
    // libJPEG
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1]; // Stores one row

    // Setup the jpeg error handler
    cinfo.err = jpeg_std_error(&jerr);

    // Image number (random as specified in the docs... :p)
    int img_num = rand() % 9999 + 1;

    // Allocate memory for the camera image. We know it's up to a 640x480 camera,
    // so at 3 bytes per pixel (max), allocate 1MB, might be wasteful, but safe
    response = (unsigned char*)calloc(1, RI_CAMERA_MAX_IMG_SIZE);

    // Get the camera image
    sprintf(cmd, "Jpeg/CamImg%i.jpg", img_num);
    data_sz = httpRequest(&ri, cmd, (char *)response, RI_CAMERA_MAX_IMG_SIZE, false);
    if(data_sz == 0) {
        free(response);
        cvZero(image);
        return RI_RESP_FAILURE;
    }

#ifdef DEBUG_DUMP_JPEG
    FILE *f;
    f = fopen("test.jpg", "w");
    fwrite(response, 1, data_sz, f);
    fclose(f);
#endif

    // Allocate JPEG decompression object
    jpeg_create_decompress(&cinfo);

    // Make the JPEG library read from memory (the image we captured from the camera)
    if(jj_mem_src(&cinfo, response, data_sz) == RI_RESP_FAILURE) {
        // Cleanup
        jpeg_destroy_decompress(&cinfo);
        free(response);
        printf("Unable to set up the decompression routine\n");
        cvZero(image);
        return RI_RESP_FAILURE;
    }

    // Read the header information
    jpeg_read_header(&cinfo, TRUE);
#ifdef DEBUG_JPEG
    printf("W: %i, H: %i, NC: %i\n", cinfo.image_width, cinfo.image_height, cinfo.num_components);
#endif

    // Start decompressing
    jpeg_start_decompress(&cinfo);

    // Allocate the row buffer
    row_pointer[0] = (unsigned char *)malloc(cinfo.output_width * cinfo.num_components);

    // Copy the data into the IPL Image
    // Perhaps fix this to just memcpy?
    for(row=0; row<cinfo.output_height; row++) {
        // Read in a line
        jpeg_read_scanlines(&cinfo, row_pointer, 1);

#ifdef JPEG_NO_MEMCPY
        // Insert it into the IplImage
        for(col=0; col<cinfo.image_width; col++){
            // RGB to BGR
            s.val[2] = row_pointer[0][col * cinfo.num_components + 0];
            s.val[1] = row_pointer[0][col * cinfo.num_components + 1];
            s.val[0] = row_pointer[0][col * cinfo.num_components + 2];
            cvSet2D(image, cinfo.output_scanline - 1, col, s);
        }



#else
        memcpy((uint8_t*)image->imageData + image->widthStep*row, row_pointer[0], cinfo.output_width * cinfo.output_components);
#endif
    }

#ifndef JPEG_NO_MEMCPY
    // Convert to the expected BGR format
    cvCvtColor(image, image, CV_RGB2BGR);
#endif

    // Completed!
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    // Cleanup
    free(row_pointer[0]);
    free(response);
    return RI_RESP_SUCCESS;
}

/**
 * @param image image is IplImage format used by OpenCV.
 * @return  RI_RESP_SUCCESS;
 * @note save an image from the camera in the IplImage format used by OpenCV.
 */
int RobotInterface::saveImage(IplImage *image){
    // Create an image to store the image from the camera
    image = cvCreateImage(cvSize(640, 480), IPL_DEPTH_8U, 3);
    getImage(image);
    time_t t = 0;
    char cvLog[21] = {0};
    char str[20] = {0};
    time(&t);
    struct tm *ptm = localtime(&t);
    strftime(str,sizeof(str),"%Y%m%d%H%M%S",ptm);
    //printf the time that catched by PC
    printf("%s\n",str);
    //cvLog is the path and the file name, the file name is just the time you have just catched
    sprintf(cvLog,"/home/zzy/Rovio-image/%s.jpeg",str);
    cvSaveImage(cvLog , image);
  //reserve the image in a folder that you can create it
    FILE *fp = NULL;
    char fname[32];
    char filename[50];
    printf("Please enter the file's name\n");
    scanf("%s",fname);
    strcat(fname,".txt");
    sprintf(filename,"/home/zzy/Rovio-image/%s",fname);
    fp = fopen(filename,"a+");
    if(fp==NULL){
        printf("file open failed!");
        return -1;
    }
    fprintf(fp,"%s\n",str);
    fclose(fp);

    return RI_RESP_SUCCESS;
}

// Returns a sequence of squares detected on the image
/**
 * @param img image is IplImage format used by OpenCV.
 * @param threshold The minimum square size argument gives the threshold for the smallest square the detector will detect.
 * @return Takes a 1 plane image and returns a list of the squares in an image.
 * @note Each squares_t structure contains the center of the square as a CvPoint named center and the area of the square as an int named area, along with a pointer to the next square in the list, named next. This pointer will be NULL at the end of the list.
 */
SquaresType *RobotInterface::findSquares(IplImage* img, int threshold) {
	CvSeq* contours;
	CvMemStorage *storage;
	int i, j, area;
	CvPoint ul, lr, pt, centroid;
	CvSize sz = cvSize( img->width, img->height);
	IplImage * canny = cvCreateImage(sz, 8, 1);
    SquaresType *sq_head, *sq, *sq_last;
    	CvSeqReader reader;
    
	// Create storage
	storage = cvCreateMemStorage(0);
	
	// Pyramid images for blurring the result
	IplImage* pyr = cvCreateImage(cvSize(sz.width/2, sz.height/2), 8, 1);
	IplImage* pyr2 = cvCreateImage(cvSize(sz.width/4, sz.height/4), 8, 1);

	CvSeq* result;
	double s, t;

	// Create an empty sequence that will contain the square's vertices
	CvSeq* squares = cvCreateSeq(0, sizeof(CvSeq), sizeof(CvPoint), storage);
    
	// Select the maximum ROI in the image with the width and height divisible by 2
	cvSetImageROI(img, cvRect(0, 0, sz.width, sz.height));
    
	// Down and up scale the image to reduce noise
	cvPyrDown( img, pyr, CV_GAUSSIAN_5x5 );
	cvPyrDown( pyr, pyr2, CV_GAUSSIAN_5x5 );
	cvPyrUp( pyr2, pyr, CV_GAUSSIAN_5x5 );
	cvPyrUp( pyr, img, CV_GAUSSIAN_5x5 );

	// Apply the canny edge detector and set the lower to 0 (which forces edges merging) 
	cvCanny(img, canny, 0, 50, 3);
		
	// Dilate canny output to remove potential holes between edge segments 
	cvDilate(canny, canny, 0, 2);

#ifdef DEBUG_SQUARE_CANNY
	cvShowImage("Debug - CANNY", canny);
#endif
		
	// Find the contours and store them all as a list
	cvFindContours(canny, storage, &contours, sizeof(CvContour), CV_RETR_LIST, CV_CHAIN_APPROX_SIMPLE, cvPoint(0,0));
            
	// Test each contour to find squares
	while(contours) {
		// Approximate a contour with accuracy proportional to the contour perimeter
		result = cvApproxPoly(contours, sizeof(CvContour), storage, CV_POLY_APPROX_DP, cvContourPerimeter(contours)*0.10, 0 );
                // Square contours should have
		//	* 4 vertices after approximation
		// 	* Relatively large area (to filter out noisy contours)
		// 	* Ne convex.
		// Note: absolute value of an area is used because
		// area may be positive or negative - in accordance with the
		// contour orientation
                if(result->total == 4 && fabs(cvContourArea(result,CV_WHOLE_SEQ,0)) > threshold && cvCheckContourConvexity(result)) {
			s=0;
                    	for(i=0; i<5; i++) {
                        	// Find the minimum angle between joint edges (maximum of cosine)
				if(i >= 2) {
                    t = fabs(riAngle((CvPoint*)cvGetSeqElem(result, i),(CvPoint*)cvGetSeqElem(result, i-2),(CvPoint*)cvGetSeqElem( result, i-1 )));
					s = s > t ? s : t;
        	                }
			}
                    
			// If cosines of all angles are small (all angles are ~90 degree) then write the vertices to the sequence 
			if( s < 0.2 ) {
				for( i = 0; i < 4; i++ ) {
					cvSeqPush(squares, (CvPoint*)cvGetSeqElem(result, i));
				}
			}
                }
                
                // Get the next contour
		contours = contours->h_next;
	}

    	// initialize reader of the sequence
	cvStartReadSeq(squares, &reader, 0);
	sq_head = NULL; sq_last = NULL; sq = NULL;
	// Now, we have a list of contours that are squares, find the centroids and area
	for(i=0; i<squares->total; i+=4) {
		// Find the upper left and lower right coordinates
		ul.x = 1000; ul.y = 1000; lr.x = 0; lr.y = 0;
		for(j=0; j<4; j++) {
			CV_READ_SEQ_ELEM(pt, reader);
			// Upper Left
			if(pt.x < ul.x)
				ul.x = pt.x;
			if(pt.y < ul.y)
				ul.y = pt.y;
			// Lower right
			if(pt.x > lr.x)
				lr.x = pt.x;
			if(pt.y > lr.y)
				lr.y = pt.y;
		}

		// Find the centroid
		centroid.x = ((lr.x - ul.x) / 2) + ul.x;
		centroid.y = ((lr.y - ul.y) / 2) + ul.y;

		// Find the area
		area = (lr.x - ul.x) * (lr.y - ul.y);

		// Add it to the storage
        sq = new SquaresType;
		// Fill in the data
		sq->area = area;
		sq->center.x = centroid.x;
		sq->center.y = centroid.y;
		sq->next = NULL;
		if(sq_last == NULL) 
			sq_head = sq;	
		else 
			sq_last->next = sq;
		sq_last = sq;
	}	
    
	// Release the temporary images and data
	cvReleaseImage(&canny);
	cvReleaseImage(&pyr);
	cvReleaseImage(&pyr2);
	cvReleaseMemStorage(&storage);
	return sq_head;
}

// Configure the camera
/**
 * @param brightness given as an integer between 0 and 0x7F, with 0x7F being the brightest and 0 as the dimmest. The default brightness is defined as RI_CAMERA_DEFAULT_BRIGHTNESS.
 * @param contrast given as an integer between 0 and 0x7F, with 0x7F as the highest contrast and 0 as the lowest. RI_CAMERA_DEFAULT_CONTRAST is the default contrast value.
 * @param framerate an integer between 2 and 32. A value of 5 is reasonable for this class.
 * @param resolution
 * <pre>
 *    Resolution Constant and	Behavior
 *    RI_CAMERA_RES_176 	Sets the camera resolution to 176x144 (QCIF)
 *    RI_CAMERA_RES_320 	Sets the camera resolution to 320x240 (QVGA)
 *    RI_CAMERA_RES_352 	Sets the camera resolution to 352x288 (CIF)
 *    RI_CAMERA_RES_640 	Sets the camera resolution to 640x480 (VGA)
 * </pre>
 * @param quality
 * <pre>
 *   Resolution Constant and	Behavior
 *   RI_CAMERA_RES_176 	Sets the camera resolution to 176x144 (QCIF)
 *   RI_CAMERA_RES_320 	Sets the camera resolution to 320x240 (QVGA)
 *   RI_CAMERA_RES_352 	Sets the camera resolution to 352x288 (CIF)
 *   RI_CAMERA_RES_640 	Sets the camera resolution to 640x480 (VGA)
 * </pre>
 * @return RI_RESP_FAILURE.
 * @note It should be run before any images are captured for use.
 */
int RobotInterface::cameraConfigure(int brightness, int contrast, int framerate, int resolution, int quality) {
    char cmd[32];
    char response[512];

    // Force the AGC to 1
    if(riWriteI2C(&ri, RI_CAMERA_AGC_ADDR, 0x01) != RI_RESP_SUCCESS) {
        printf("Unable to set AGC\n");
        return RI_RESP_FAILURE;
    }
    // Turn off night mode
    if(riWriteI2C(&ri, RI_CAMERA_NIGHTMODE_ADDR, 0x02) != RI_RESP_SUCCESS) {
        printf("Unable to disable night mode\n");
        return RI_RESP_FAILURE;
    }
    // Force the brightness to a reasonable value
    if(riWriteI2C(&ri, RI_CAMERA_BRIGHTNESS_ADDR, brightness)) {
        printf("Unable to set brightness\n");
        return RI_RESP_FAILURE;
    }
    // Force the contrast to a reasonable value
    if(riWriteI2C(&ri, RI_CAMERA_CONTRAST_ADDR, contrast)) {
        printf("Unable to set brightness\n");
        return RI_RESP_FAILURE;
    }

#if 0
    // Change the brightness
    if(brightness < 0)
        brightness = 0;
    if(brightness > 6)
        brightness = 6;
    sprintf(cmd, "ChangeBrightness.cgi?Brightness=%i", brightness);
    http_request(ri, cmd, response, 512, false);
    // No response (pg. 23 of the API)
#endif

    // Set the camera resolution
    if(resolution < 0)
        resolution = 0;
    if(resolution > 3)
        resolution = 3;
    sprintf(cmd, "ChangeResolution.cgi?ResType=%i", resolution);
    httpRequest(&ri, cmd, response, 512, false);
    // No response

    // Set the camera quality
    if(quality < 0)
        quality = 0;
    if(quality > 2)
        quality = 2;
    sprintf(cmd, "ChangeCompressRatio.cgi?Ratio=%i", quality);
    httpRequest(&ri, cmd, response, 512, false);
    // No response

    // Set the camera framerate
    if(framerate < 2)
        framerate = 2;
    if(framerate > 32)
        framerate = 32;
    sprintf(cmd, "ChangeFramerate.cgi?Framerate=%i", framerate);
    httpRequest(&ri, cmd, response, 512, false);
    // No response

    return RI_RESP_SUCCESS;
}

/**
 * @param mic_volume Configure the microphone's volume.
 * @param speaker_volume Configure the speaker's volume.
 * @return RI_RESP_SUCCESS.
 */
int RobotInterface::volumeConfigure(int mic_volume, int speaker_volume) {
    char cmd[32];
    char response[512];

    // Change the microphone volume
    if(mic_volume < 0)
        mic_volume = 0;
    if(mic_volume > 31)
        mic_volume = 31;
    sprintf(cmd, "ChangeMicVolume.cgi?MicVolume=%i", mic_volume);
    httpRequest(&ri, cmd, response, 512, false);

    // Change the speaker volume
    if(speaker_volume < 0)
        speaker_volume = 0;
    if(speaker_volume > 31)
        speaker_volume = 31;
    sprintf(cmd, "ChangeSpeakerVolume.cgi?SpeakerVolume=%i", speaker_volume);
    httpRequest(&ri, cmd, response, 512, false);

    return RI_RESP_SUCCESS;
}

// Game API
/**
 * @param x  X coordinate of the robot.
 * @param y  Y coordinate of the robot.
 * @return the robot's coordinate on the map.
 * @note the location the robot is moving to MUST be reserved first.
 */
int RobotInterface::updateMap(int x, int y) {
    char response[512];
    char cmd[128];
    int ret = 0;

    // Send the update command
    sprintf(cmd, "cgi-bin/game.cgi?UPDATE=%i&X=%i&Y=%i", (&ri)->id, x, y);
    httpRequest(&ri, cmd, response, 512, true);
    sscanf(response, "Result=%i", &ret);
    return ret;
}

/**
 * @param x  X coordinate of the robot.
 * @param y  Y coordinate of the robot.
 * @return Reserves the robot's coordinate on the map.
 */
int RobotInterface::reserveMap(int x, int y) {
        char response[512];
        char cmd[128];
        int ret = 0;

        // Send the reserve command
        sprintf(cmd, "cgi-bin/game.cgi?RESERVE=%i&X=%i&Y=%i", (&ri)->id, x, y);
        httpRequest(&ri, cmd, response, 512, true);
        sscanf(response, "Result=%i", &ret);
        return ret;
}

/**
 * @param score1
 * @param score2
 * @note Gets the current map from the server. Each map object is in a linked list, in order, by row. So, 0,0 is first, followed by 1,0, then 2,0, etc. All squares are represented by the type below:
* <pre>
* Map Object Structure
* Structure Element and	Function
* x 	        X Location
* y 	        Y Location
* type  	Object Type (see below)
* points 	Point value of this location
* next   	Next element in the list
* Map Object Types
* Type   and 	Function
*MAP_OBJ_EMPTY  	Empty Space
*MAP_OBJ_ROBOT_1 	Robot 1's location
*MAP_OBJ_ROBOT_2 	Robot 2's location
*MAP_OBJ_PELLET 	Point Pellet
*MAP_OBJ_POST 	Post in the map
*MAP_OBJ_RESERVE_1 	Robot 1's reserved location it's moving to
*MAP_OBJ_RESERVE_2 	Robot 2's reserved location it's moving to
* </pre>
 */
MapObjType *RobotInterface::getMap(int *score1, int *score2) {
        MapObjType *map_start = NULL, *mo, *last_mo;
        char response[512];
        char cmd[128];
        char *tok;
        char *map[MAP_MAX_Y];
        int x,y,value;

        // Get the map
        sprintf(cmd, "cgi-bin/game.cgi?MAP");
        httpRequest(&ri, cmd, response, 512, true);
        sscanf(response, "%i:%i\n", score1, score2);

            // Read in the map data
        strtok(response, "\n");
            for(y=0; y<MAP_MAX_Y; y++) {
                    // Get a line
            tok = strtok(NULL, "\n");
            if(tok != NULL)
                map[y] = tok;
        }

        // Now parse the lines
        for(y=0; y<MAP_MAX_Y; y++) {
                    tok = strtok(map[y], ",");
                    /* Parse the line */
                    x = 0;
                    while(tok != NULL && tok[0] != '\n') {
                            sscanf(tok, "%X,", &value);

                            // Allocate a map object
                            mo = (MapObjType *) malloc(sizeof(MapObjType));

                            // Assign the map object values
                            mo->x = x;
                            mo->y = y;
                            mo->type = MAP_OBJ_TYPE(value);
                            if(mo->type == MAP_OBJ_PELLET)
                                    mo->points = MAP_OBJ_SCORE(value);
                            else
                                    mo->points = 0;

                            // Assign the map objects
                            if(map_start == NULL)
                                    map_start = mo;
                            else
                                    last_mo->next = mo;

                            // Clear the next pointer
                            mo->next = NULL;

                            // Save the map object for the next pointer
                            last_mo = mo;

                            // Get the next token
                            tok = strtok(NULL, ",");
                            x++;
                    }
            }

        return map_start;
}
