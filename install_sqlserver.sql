IF OBJECT_ID(N'accounts', 'U') is null
BEGIN
    CREATE TABLE [accounts]
    (
        [username]         VARCHAR(16) NOT NULL,
        [password]         VARCHAR(64) NOT NULL,
        [fullname]         VARCHAR(64) NOT NULL,
        [location]         VARCHAR(64) NOT NULL,
        [email]            VARCHAR(64) NOT NULL,
        [computer]         VARCHAR(64) NOT NULL,
        [hdid]             INTEGER     NOT NULL,
        [regip]            VARCHAR(15) NOT NULL,
        [lastip]           VARCHAR(15)          DEFAULT NULL,
        [created]          INTEGER     NOT NULL,
        [lastused]         INTEGER              DEFAULT NULL,
        [password_version] INTEGER     NOT NULL DEFAULT 2,

        PRIMARY KEY ([username])
    )
END

IF OBJECT_ID(N'characters', 'U') is null
BEGIN
    CREATE TABLE [characters]
    (
        [name]        VARCHAR(16) NOT NULL,
        [account]     VARCHAR(16)          DEFAULT NULL,
        [title]       VARCHAR(32)          DEFAULT NULL,
        [home]        VARCHAR(32)          DEFAULT NULL,
        [fiance]      VARCHAR(16)          DEFAULT NULL,
        [partner]     VARCHAR(16)          DEFAULT NULL,
        [admin]       INTEGER     NOT NULL DEFAULT 0,
        [class]       INTEGER     NOT NULL DEFAULT 0,
        [gender]      INTEGER     NOT NULL DEFAULT 0,
        [race]        INTEGER     NOT NULL DEFAULT 0,
        [hairstyle]   INTEGER     NOT NULL DEFAULT 0,
        [haircolor]   INTEGER     NOT NULL DEFAULT 0,
        [map]         INTEGER     NOT NULL DEFAULT 192,
        [x]           INTEGER     NOT NULL DEFAULT 7,
        [y]           INTEGER     NOT NULL DEFAULT 6,
        [direction]   INTEGER     NOT NULL DEFAULT 2,
        [level]       INTEGER     NOT NULL DEFAULT 0,
        [exp]         INTEGER     NOT NULL DEFAULT 0,
        [hp]          INTEGER     NOT NULL DEFAULT 10,
        [tp]          INTEGER     NOT NULL DEFAULT 10,
        [str]         INTEGER     NOT NULL DEFAULT 0,
        [int]         INTEGER     NOT NULL DEFAULT 0,
        [wis]         INTEGER     NOT NULL DEFAULT 0,
        [agi]         INTEGER     NOT NULL DEFAULT 0,
        [con]         INTEGER     NOT NULL DEFAULT 0,
        [cha]         INTEGER     NOT NULL DEFAULT 0,
        [statpoints]  INTEGER     NOT NULL DEFAULT 0,
        [skillpoints] INTEGER     NOT NULL DEFAULT 0,
        [karma]       INTEGER     NOT NULL DEFAULT 1000,
        [sitting]     INTEGER     NOT NULL DEFAULT 0,
        [hidden]      INTEGER     NOT NULL DEFAULT 0,
        [nointeract]  INTEGER     NOT NULL DEFAULT 0,
        [bankmax]     INTEGER     NOT NULL DEFAULT 0,
        [goldbank]    INTEGER     NOT NULL DEFAULT 0,
        [usage]       INTEGER     NOT NULL DEFAULT 0,
        [inventory]   VARCHAR(MAX),
        [bank]        VARCHAR(MAX),
        [paperdoll]   VARCHAR(MAX),
        [spells]      VARCHAR(MAX),
        [guild]       CHAR(3)              DEFAULT NULL,
        [guild_rank]  INTEGER              DEFAULT NULL,
        [guild_rank_string] VARCHAR(16)    DEFAULT NULL,
        [quest]       VARCHAR(MAX),
        [vars]        VARCHAR(MAX),

        PRIMARY KEY ([name])
    )

    CREATE INDEX [character_account_index] ON [characters] ([account])
    CREATE INDEX [character_guild_index] ON [characters] ([guild])
END

IF OBJECT_ID(N'guilds', 'U') is null
BEGIN
    CREATE TABLE [guilds]
    (
        [tag]         CHAR(3)     NOT NULL,
        [name]        VARCHAR(32) NOT NULL,
        [description] VARCHAR(MAX),
        [created]     INTEGER     NOT NULL,
        [ranks]       VARCHAR(MAX),
        [bank]        INTEGER     NOT NULL DEFAULT 0,

        PRIMARY KEY ([tag]),
        UNIQUE      ([name])
    )
END

IF OBJECT_ID(N'bans', 'U') is null
BEGIN
    CREATE TABLE [bans]
    (
        [ip]       INTEGER              DEFAULT NULL,
        [hdid]     INTEGER              DEFAULT NULL,
        [username] VARCHAR(16)          DEFAULT NULL,
        [setter]   VARCHAR(16)          DEFAULT NULL,
        [expires]  INTEGER     NOT NULL DEFAULT 0,

        PRIMARY KEY ([ip], [hdid], [username], [expires])
    )

    CREATE INDEX [ban_ip_index] ON [bans] ([ip])
    CREATE INDEX [ban_hdid_index] ON [bans] ([hdid])
    CREATE INDEX [ban_username_index] ON [bans] ([username])
END

IF OBJECT_ID(N'reports', 'U') is null
BEGIN
    CREATE TABLE [reports]
    (
        [reporter] VARCHAR(16) NOT NULL,
        [reported] VARCHAR(16) NOT NULL,
        [reason]   VARCHAR(MAX),
        [time]     INTEGER     NOT NULL,
        [chat_log] VARCHAR(MAX)        NOT NULL,

        PRIMARY KEY ([reporter], [reported], [time])
    )
END
