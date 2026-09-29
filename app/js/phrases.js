// The pet's built-in phrase book: instant lines for every situation, used when the
// AI brain is off or still loading (and for quick reactions).

const L = {
  greet: ['Hi {owner}! I missed you!', 'Yay, you\'re here!', 'Hello hello!', 'There you are!', 'Hi! Did you bring snacks?'],
  hungry: ['My tummy is rumbling...', 'Food please?', 'I could really go for some {fav}.', 'Is it snack time yet?', 'So... hungry...'],
  sleepy: ['I\'m getting so sleepy...', 'Can you put me to bed?', '*yaaawn*', 'My eyes are heavy...'],
  bedtime: ['It\'s almost bedtime!', 'Bedtime soon... *yawn*', 'Just a few more minutes before bed!', 'Almost time to sleep, {owner}.'],
  lonely: ['Pet me? Pretty please?', 'I need a cuddle.', 'Do you still love me?', 'Hug time?'],
  bored: ['I\'m so bored. Play with me!', 'Let\'s play a game!', 'Wanna teach me a trick?', 'Entertain me!'],
  sick: ['I don\'t feel so good...', 'Achoo! I think I\'m sick.', 'Medicine... please...', 'Everything is spinny and bad.'],
  messy: ['It\'s getting messy in here.', 'Crumbs everywhere! Can you tidy up?', 'Tilt me to shake off the crumbs!'],
  yum: ['Mmm, {food}! Yummy!', 'Nom nom nom!', 'That hit the spot!', 'Tasty {food}, thank you!'],
  fav_food: ['{food}!! My absolute favorite!', 'I LOVE {food}! You know me so well!', 'Best. Snack. Ever!'],
  yuck: ['Ew, {food}... blegh!', 'Yuck! Not {food}!', 'Bleh. Please no more {food}.'],
  full: ['I\'m stuffed! No more {food}!', 'Too full... can\'t eat another bite.', 'Maybe later!'],
  burp: ['*buuurp* ...excuse me!', 'Oops! Hehe, pardon me.'],
  purr: ['Mmm, that feels nice.', 'More pets please!', 'You give the best pets.', 'Purrrr...'],
  tickle: ['Hehe! That tickles!', 'Stop it, haha!', 'Heehee!'],
  boop: ['Boop!', 'Hey! Hehe.', 'You booped me!', 'Beep!'],
  dizzy: ['Whoa... the room is spinning.', 'So dizzy...', 'Everything is wobbly!'],
  stop: ['Hey! Stop shaking me!', 'Okay, that\'s enough shaking!', 'I\'m gonna be sick!'],
  scared: ['Aaaah!', 'Whoa, I\'m falling!', 'Eeek!'],
  whee: ['Wheee! Again!', 'Woohoo!', 'That was fun!'],
  peekaboo: ['Peekaboo!', 'Found me!', 'Hehe, there you are!'],
  upside_down: ['Whoa, the world is upside down!', 'Hey, which way is up?'],
  picked_up: ['Oh! Hi!', 'Where are we going?', 'Up we go!'],
  rocked: ['Mmm, so cozy...', 'Rock-a-bye...', 'This is nice.'],
  goodnight: ['Goodnight, {owner}.', 'Nighty night...', 'Sweet dreams...'],
  nap: ['Just a little nap...', 'Zzz...', 'Resting my eyes...'],
  morning: ['Good morning!', 'Rise and shine!', 'What a great sleep!'],
  wake: ['I\'m awake!', 'That was a good nap!', 'Hi again!'],
  grumpy: ['Hey! I was sleeping!', 'Five more minutes...', 'Hmph. Rude.'],
  not_sleepy: ['I\'m not tired!', 'But I\'m not sleepy yet!', 'Lights on, please!'],
  asleep: ['Zzz...', 'Mmm... five more minutes...'],
  trick_fail: ['Oops! That didn\'t go right.', 'Hmm, how does it go again?', 'Whoops!', 'I almost had it!'],
  try_again: ['I\'ll get it next time!', 'Let me try again!', 'Practice makes perfect!'],
  praised: ['Yay! Did I do good?', 'Thank you!', 'I\'m the best!', 'Hehe!'],
  learned: ['I did it! I learned {trick}!', 'I\'m a {trick} master now!', 'Look at me! {trick}!'],
  show_off: ['Watch this! {trick}!', 'Hey, look what I can do!', 'Ta-da!'],
  too_sick: ['I feel too sick for that...', 'Not now, I don\'t feel well.'],
  too_tired: ['I\'m too tired...', 'Maybe after a nap?'],
  stage_up: ['Look! I\'m a {stage} now!', 'I\'m growing up!', 'I grew! Did you see?'],
  hatched: ['Hello world! I\'m {name}!', 'Hi! Are you my {owner}?', 'I\'m born! Hi hi hi!'],
  cured: ['I feel all better!', 'Healthy again! Thank you!', 'Woohoo, no more sniffles!'],
  medicine: ['Bleh! That tastes awful.', 'Yuck! But okay...', 'I wasn\'t even sick!'],
  clean_thanks: ['So sparkly clean!', 'Ahh, much better!', 'Thanks for tidying up!'],
  new_name: ['{name}? I love it!', 'My name is {name}! Hehe.', '{name}! That\'s me!'],
  new_look: ['Ooh, do I look fancy?', 'Looking good!', 'I love my new look!'],
  record: ['New record: {score}! I\'m amazing!', 'High score! {score}!'],
  game_win: ['I win! Hehe, just kidding, you win!', 'That was so fun!', 'Again, again!'],
  game_lose: ['Aww, so close!', 'Let\'s play again!', 'Next time for sure!'],
  muse: ['I wonder what clouds taste like.', 'Do you think pixels dream?', 'Being two eyes is pretty great.',
    'What\'s your favorite food? Mine is {fav}.', 'I love you, {owner}.', 'If I had hands, I\'d give you a high five.',
    'Sometimes I blink just for fun.', 'I think I\'m getting smarter every day.'],
};

// Babies talk in baby.
const BABY = {
  greet: ['Hi hi!', 'Goo! You!', 'Yay!'],
  hungry: ['Food? Food!', 'Num num?', 'Hungee...'],
  bedtime: ['Nigh-nigh soon...', 'Sleepy time?'],
  sleepy: ['Sleepy...', 'Nigh-nigh?'],
  lonely: ['Uppy? Cuddle?', 'Pet pet?'],
  bored: ['Play! Play!', 'Bored...'],
  yum: ['Num num num!', 'Yummy!'],
  fav_food: ['{food}!! Yay yay!'],
  yuck: ['Bleh!', 'No no no!'],
  purr: ['Hehe...', 'Mmm...'],
  muse: ['Goo goo?', 'Blink blink!', 'Wawa!'],
};

// Things the pet says when it just feels like talking: questions for you, little stories, jokes.
const CHAT = {
  ask: ['What did you do today, {owner}?', 'What\'s your favorite color? Mine is glowing blue.', 'If you had a superpower, what would it be?',
    'What should we do tomorrow?', 'What\'s the best snack in the whole world?', 'Do you have any friends I should meet?',
    'What music do you like?', 'Where would you go if you could go anywhere?', 'What made you smile today?',
    'Do you think I\'d be good at soccer?', 'What are you up to right now?', 'Can you teach me a new word?'],
  tell: ['I had the weirdest dream about {fav}.', 'I counted my pixels today. There are so many!', 'I practiced blinking. I\'m getting really good at it.',
    'Guess what? You\'re my favorite human.', 'I tried to wink with both eyes. It didn\'t work.', 'I wonder what clouds taste like.',
    'Sometimes I blink just for fun.', 'I think I\'m getting smarter every day.'],
  joke: ['Why did the robot go on vacation? To recharge its batteries!', 'What do you call a sleepy pixel? A nap-sized byte!',
    'Knock knock! Oh wait, I don\'t have hands. Hehe.', 'Why was the computer cold? It left its Windows open!'],
  morning: ['Good morning, {owner}! Did you sleep well?', 'Morning! What\'s the plan today?', 'Rise and shine! I\'m wide awake!'],
  evening: ['What was the best part of your day?', 'Evenings are cozy. What are you up to?', 'Did you have a good day, {owner}?'],
  late: ['It\'s getting late... are you sleepy too?', 'Shouldn\'t we both be asleep? Hehe.'],
};
const BABY_CHAT = ['Goo? Play?', 'Hehe! You!', 'Wawa goo!', 'Blink blink!', 'Pat pat?'];

const pick = (a) => a[Math.floor(Math.random() * a.length)];

// Something to start a conversation with: { text, emotion, idea }, where idea is the same
// thought in words for the AI brain.
export function starter(ctx) {
  const h = new Date().getHours();
  const time = h >= 5 && h < 11 ? 'morning' : h >= 17 && h < 22 ? 'evening' : h >= 22 || h < 5 ? 'late' : null;
  const kinds = ['ask', 'ask', 'tell', 'joke'];
  if (time) kinds.push(time);
  const kind = pick(kinds);
  const pool = ctx.stage === 1 ? BABY_CHAT : CHAT[kind];
  const text = pick(pool).replace(/\{(\w+)\}/g, (_, k) => ctx[k] ?? '').replace(/\s+/g, ' ').trim();
  // Direct instructions: small models ask real questions far more often when told to.
  const o = ctx.owner;
  const idea = {
    ask: `Ask ${o} one fun, friendly question about their day or the things they like. End with a question mark.`,
    tell: `Tell ${o} one tiny, cute or silly thought you just had.`,
    joke: `Tell ${o} a very short, silly, kid-friendly joke.`,
    morning: `Say good morning to ${o} and ask how they slept. End with a question mark.`,
    evening: `Ask ${o} how their day went. End with a question mark.`,
    late: `Notice that it is getting late and ask ${o} if they are sleepy too. End with a question mark.`,
  }[kind];
  return { text, emotion: kind === 'joke' ? 'joy' : kind === 'ask' ? 'happy' : 'thinking', idea };
}

export function line(intent, ctx) {
  const pool = (ctx.stage === 1 && BABY[intent]) || L[intent] || L.muse;
  const text = pick(pool).replace(/\{(\w+)\}/g, (_, k) => ctx[k] ?? '');
  return { text: text.replace(/\s+/g, ' ').trim(), emotion: emotionFor(intent) };
}

export function emotionFor(intent) {
  return {
    greet: 'happy', hungry: 'sad', sleepy: 'sleepy', lonely: 'sad', bored: 'sad', sick: 'cry', yum: 'happy',
    fav_food: 'love', yuck: 'angry', purr: 'love', tickle: 'joy', dizzy: null, stop: 'angry', scared: 'scared',
    whee: 'joy', peekaboo: 'joy', grumpy: 'angry', learned: 'stars', show_off: 'smug', stage_up: 'stars',
    hatched: 'joy', cured: 'stars', new_name: 'joy', new_look: 'smug', record: 'stars', praised: 'joy',
    trick_fail: 'sad', muse: 'thinking', bedtime: 'sleepy',
  }[intent] ?? null;
}

// What happened, in words, for the AI brain.
export function describe(intent, ctx) {
  return {
    greet: `${ctx.owner} just opened the app to visit you`,
    hungry: 'you are getting hungry and want food',
    bedtime: 'it is almost your bedtime and you are getting sleepy',
    sleepy: 'you are getting very sleepy',
    lonely: 'you feel lonely and want to be petted',
    bored: 'you are bored and want to play',
    sick: 'you just got sick and feel awful',
    messy: 'there are crumbs everywhere around you',
    yum: `${ctx.owner} fed you ${ctx.food}`,
    fav_food: `${ctx.owner} fed you ${ctx.food}, your favorite food in the world`,
    yuck: `${ctx.owner} fed you ${ctx.food}, which you hate`,
    full: `${ctx.owner} tried to feed you ${ctx.food} but you are completely full`,
    burp: 'you just burped after eating',
    purr: `${ctx.owner} is petting you`,
    tickle: `${ctx.owner} is tickling you`,
    boop: `${ctx.owner} booped you`,
    dizzy: 'you just got shaken and feel dizzy',
    stop: 'you have been shaken way too much and you are annoyed',
    scared: 'you are falling through the air',
    whee: 'you got spun or tossed around and loved it',
    peekaboo: `${ctx.owner} just played peekaboo with you`,
    upside_down: 'you have been turned upside down',
    picked_up: `${ctx.owner} just picked you up`,
    rocked: `${ctx.owner} is gently rocking you`,
    goodnight: 'you are falling asleep for the night',
    nap: 'you are dozing off for a nap',
    morning: 'you just woke up after a good sleep',
    wake: 'you just woke up from a nap',
    grumpy: 'you were woken up too early and you are grumpy',
    not_sleepy: 'the lights were turned off but you are not sleepy',
    trick_fail: 'you just messed up a trick',
    try_again: 'you messed up a trick but you are still trying',
    praised: `${ctx.owner} praised you`,
    learned: `you just mastered the trick "${ctx.trick}"`,
    show_off: `you are showing off your "${ctx.trick}" trick`,
    too_sick: 'you are asked to do something but you feel too sick',
    too_tired: 'you are asked to do something but you are too tired',
    stage_up: `you just grew up into a ${ctx.stage}`,
    hatched: 'you just hatched from your egg and are seeing the world for the first time',
    cured: 'medicine made you feel better',
    medicine: 'you were given yucky medicine',
    clean_thanks: `${ctx.owner} cleaned up the crumbs around you`,
    new_name: `${ctx.owner} just named you ${ctx.name}`,
    new_look: `${ctx.owner} gave you a new look: ${ctx.wearing}`,
    record: `you got a new high score of ${ctx.score} in Snack Catch`,
    game_win: 'you just finished a game and it went great',
    game_lose: 'you just finished a game and lost',
    muse: 'nothing much is happening; share a random cute thought',
  }[intent] || `something happened (${intent})`;
}
